#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YOLOv8 表情识别推理脚本（支持 .pt / .onnx）
用法:
  python inference.py --image <img_path> [--model <model_path>]

.pt 模型 → ultralytics YOLO 管道
.onnx 模型 → 直接 onnxruntime，绕过 AutoBackend（兼容量化与非量化）
"""

import sys
import os
import argparse
import traceback

import cv2
import numpy as np
from PIL import Image, ImageDraw, ImageFont

# 抑制 matplotlib 缓存目录警告
os.environ.setdefault("MPLCONFIGDIR", "/tmp/matplotlib_zyx")


# ---------------- YOLOv8 ONNX 推理核心 ----------------

CLASS_NAMES = ["angry", "disgust", "fear", "happy", "sad", "surprise", "neutral"]
CLASS_ZH = {"angry": "愤怒", "disgust": "厌恶", "fear": "恐惧",
            "happy": "开心", "sad": "悲伤", "surprise": "惊讶", "neutral": "中性"}


def find_chinese_font():
    """查找系统中可用的中文字体"""
    candidates = [
        # macOS
        "/System/Library/Fonts/PingFang.ttc",
        "/System/Library/Fonts/STHeiti Light.ttc",
        "/System/Library/Fonts/STHeiti Medium.ttc",
        "/Library/Fonts/Arial Unicode.ttf",
        "/System/Library/Fonts/Supplemental/Songti.ttc",
        "/System/Library/Fonts/Supplemental/Arial Unicode.ttf",
        # Linux
        "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc",
        "/usr/share/fonts/truetype/wqy/wqy-microhei.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        # Windows (if running on Windows)
        "C:/Windows/Fonts/msyh.ttc",
        "C:/Windows/Fonts/simsun.ttc",
    ]
    for path in candidates:
        if os.path.exists(path):
            return path
    # 回退：尝试用 pillow 默认字体
    return None


def draw_chinese_text(img_cv, text, position, font_path, font_size=20, color=(0, 255, 0)):
    """用 PIL 在 OpenCV 图片上绘制中文文本"""
    # OpenCV BGR → PIL RGB
    img_rgb = cv2.cvtColor(img_cv, cv2.COLOR_BGR2RGB)
    pil_img = Image.fromarray(img_rgb)
    draw = ImageDraw.Draw(pil_img)

    if font_path and os.path.exists(font_path):
        try:
            font = ImageFont.truetype(font_path, font_size)
        except Exception:
            font = ImageFont.load_default()
    else:
        font = ImageFont.load_default()

    # 添加文字背景（深色半透明）
    bbox = draw.textbbox(position, text, font=font)
    padding = 3
    bg_rect = [bbox[0] - padding, bbox[1] - padding,
               bbox[2] + padding, bbox[3] + padding]
    draw.rectangle(bg_rect, fill=(0, 0, 0, 180))
    draw.text(position, text, font=font, fill=(color[2], color[1], color[0]))  # PIL uses RGB

    # 转回 OpenCV BGR
    result = cv2.cvtColor(np.array(pil_img), cv2.COLOR_RGB2BGR)
    return result


def letterbox(img, new_shape=(640, 640), color=(114, 114, 114)):
    """缩放+填充，保持宽高比，返回 (img, ratio, dw, dh)"""
    shape = img.shape[:2]
    r = min(new_shape[0] / shape[0], new_shape[1] / shape[1])
    new_unpad = int(round(shape[1] * r)), int(round(shape[0] * r))
    dw, dh = new_shape[1] - new_unpad[0], new_shape[0] - new_unpad[1]
    dw /= 2
    dh /= 2
    img = cv2.resize(img, new_unpad, interpolation=cv2.INTER_LINEAR)
    top, bottom = int(round(dh - 0.1)), int(round(dh + 0.1))
    left, right = int(round(dw - 0.1)), int(round(dw + 0.1))
    img = cv2.copyMakeBorder(img, top, bottom, left, right,
                             cv2.BORDER_CONSTANT, value=color)
    return img, r, dw, dh


def postprocess(output0, orig_shape, ratio, dw, dh, conf_thres=0.25, iou_thres=0.45):
    """YOLOv8 检测头后处理（NMS）"""
    # output0: shape (1, 84, 8400)
    preds = np.squeeze(output0[0]).T  # (8400, 84)
    boxes = preds[:, :4]
    scores_raw = preds[:, 4:]
    class_ids = scores_raw.argmax(axis=1)
    scores = scores_raw.max(axis=1)

    keep = scores > conf_thres
    boxes, scores, class_ids = boxes[keep], scores[keep], class_ids[keep]

    if len(boxes) == 0:
        return [], [], []

    # xywh → xyxy
    boxes_xyxy = boxes.copy()
    boxes_xyxy[:, 0] = boxes[:, 0] - boxes[:, 2] / 2  # x1
    boxes_xyxy[:, 1] = boxes[:, 1] - boxes[:, 3] / 2  # y1
    boxes_xyxy[:, 2] = boxes[:, 0] + boxes[:, 2] / 2  # x2
    boxes_xyxy[:, 3] = boxes[:, 1] + boxes[:, 3] / 2  # y2

    # 转回原图坐标
    boxes_xyxy[:, [0, 2]] -= dw
    boxes_xyxy[:, [1, 3]] -= dh
    boxes_xyxy /= ratio

    # 裁剪到图像范围内
    h, w = orig_shape
    boxes_xyxy[:, 0] = np.clip(boxes_xyxy[:, 0], 0, w)
    boxes_xyxy[:, 1] = np.clip(boxes_xyxy[:, 1], 0, h)
    boxes_xyxy[:, 2] = np.clip(boxes_xyxy[:, 2], 0, w)
    boxes_xyxy[:, 3] = np.clip(boxes_xyxy[:, 3], 0, h)

    # NMS
    indices = cv2.dnn.NMSBoxes(
        boxes_xyxy.tolist(), scores.tolist(), conf_thres, iou_thres
    )

    if len(indices) == 0:
        return [], [], []

    result_boxes = boxes_xyxy[indices.flatten()]
    result_scores = scores[indices.flatten()]
    result_cls = class_ids[indices.flatten()]

    return result_boxes, result_scores, result_cls


def infer_onnx(img_path, model_path):
    """纯 onnxruntime 推理（兼容量化和非量化模型）

    量化模型含 INT8 算子（ConvInteger 等），CoreML 无法处理，
    因此量化模型强制 CPU-only 并启用图优化以确保兼容。
    """
    import onnxruntime as ort

    if not os.path.exists(img_path):
        print(f"[ERROR] 图片不存在: {img_path}", flush=True)
        sys.exit(1)

    img = cv2.imread(img_path)
    if img is None:
        print(f"[ERROR] 无法读取图片: {img_path}", flush=True)
        sys.exit(1)

    orig_shape = img.shape[:2]

    # 预处理
    img_pp, ratio, dw, dh = letterbox(img, (640, 640))
    blob = img_pp[:, :, ::-1].transpose(2, 0, 1)  # HWC → CHW, BGR → RGB
    blob = np.ascontiguousarray(blob, dtype=np.float32) / 255.0
    blob = np.expand_dims(blob, axis=0)

    # 判断是否为量化模型
    is_quantized = "quantized" in os.path.basename(model_path).lower()

    # ---- 加载模型 ----
    sess = None
    last_error = ""

    if is_quantized:
        # 量化模型：优先尝试 CPU，如失败则尝试 CoreML+CPU
        # 旧版量化可能含 ConvInteger（仅 CPU 支持），新版权重量化可用 CoreML
        providers_to_try = [
            ["CPUExecutionProvider"],
            ["CoreMLExecutionProvider", "CPUExecutionProvider"],
        ]
    else:
        # 非量化模型：CoreML 优先（macOS 加速），CPU 兜底
        providers_to_try = [
            ["CoreMLExecutionProvider", "CPUExecutionProvider"],
            ["CPUExecutionProvider"],
        ]

    for providers in providers_to_try:
        try:
            opts = ort.SessionOptions()
            opts.graph_optimization_level = ort.GraphOptimizationLevel.ORT_ENABLE_ALL
            # 量化模型禁用内存 arena，避免 INT8 内存对齐问题
            if is_quantized:
                opts.enable_cpu_mem_arena = False
            sess = ort.InferenceSession(model_path, opts, providers=providers)
            print(f"[INFO] 模型加载成功，提供器: {sess.get_providers()}", flush=True)
            break
        except Exception as e:
            last_error = str(e)
            continue

    if sess is None:
        print(f"[ERROR] ❌ 无法加载 ONNX 模型: {model_path}", flush=True)
        print(f"[ERROR] 错误详情: {last_error}", flush=True)
        if is_quantized:
            print(f"[HINT] 💡 量化模型加载失败，可能原因:", flush=True)
            print(f"[HINT]   1. onnxruntime 版本过旧 → pip install --upgrade onnxruntime", flush=True)
            print(f"[HINT]   2. 量化算子兼容性 → 尝试重新量化或使用 .pt 模型推理", flush=True)
        sys.exit(1)

    # ---- 执行推理 ----
    input_name = sess.get_inputs()[0].name
    try:
        outputs = sess.run(None, {input_name: blob})
    except Exception as e:
        err_msg = str(e)
        print(f"[ERROR] ❌ 推理执行失败", flush=True)
        print(f"[ERROR] 详情: {err_msg}", flush=True)
        if is_quantized:
            if "ConvInteger" in err_msg or "int" in err_msg.lower():
                print(f"[HINT] 💡 量化 INT8 算子不被当前 onnxruntime 支持", flush=True)
                print(f"[HINT] 请尝试: pip install --upgrade onnxruntime", flush=True)
            else:
                print(f"[HINT] 💡 量化模型推理异常，可能是 OP 兼容性问题", flush=True)
            print(f"[HINT] 替代方案: 使用训练后的 .pt 模型进行推理", flush=True)
        sys.exit(1)

    # 后处理
    boxes, scores, class_ids = postprocess(outputs, orig_shape, ratio, dw, dh)

    return img, boxes, scores, class_ids


def infer_pt(img_path, model_path):
    """ultralytics YOLO 管道推理 .pt 模型"""
    from ultralytics import YOLO

    if not os.path.exists(img_path):
        print(f"[ERROR] 图片不存在: {img_path}", flush=True)
        sys.exit(1)

    img = cv2.imread(img_path)
    if img is None:
        print(f"[ERROR] 无法读取图片: {img_path}", flush=True)
        sys.exit(1)

    model = YOLO(model_path)
    results = model(img_path, verbose=False)

    boxes_list, scores_list, cls_list = [], [], []
    for r in results:
        if r.boxes is not None and len(r.boxes) > 0:
            for box in r.boxes:
                boxes_list.append(box.xyxy[0].cpu().numpy())
                scores_list.append(float(box.conf[0]))
                cls_list.append(int(box.cls[0]))

    return img, boxes_list, scores_list, cls_list


def find_default_model():
    """自动查找模型（.pt 优先，因为 .onnx 量化可能不可用）"""
    base = os.path.expanduser(
        "/Users/ddd/Documents/课程作业/工程基础与创新设计/zyx/jaffe_jpg/"
        "yolo_output/runs/train/weights/"
    )
    candidates = [
        os.path.join(base, "best.pt"),
        os.path.join(base, "best.onnx"),
    ]
    for p in candidates:
        if os.path.exists(p):
            return p
    return candidates[0]


# ---------------- 主入口 ----------------

def main():
    parser = argparse.ArgumentParser(description="YOLOv8 表情识别推理")
    parser.add_argument('--image', '-i', type=str, required=True,
                        help='输入图片路径')
    parser.add_argument('--model', '-m', type=str, default=None,
                        help='模型路径 (.pt 或 .onnx)；默认自动查找')
    args = parser.parse_args()

    model_path = args.model if args.model else find_default_model()
    if not os.path.exists(model_path):
        print(f"[ERROR] 模型文件不存在: {model_path}", flush=True)
        sys.exit(1)

    print(f"[INFO] 模型: {model_path}", flush=True)
    print(f"[INFO] 图片: {args.image}", flush=True)

    try:
        is_onnx = model_path.endswith(".onnx")
        if is_onnx:
            # 检查是否量化模型且 onnxruntime 不支持
            img, boxes, scores, class_ids = infer_onnx(args.image, model_path)
        else:
            img, boxes, scores, class_ids = infer_pt(args.image, model_path)
    except Exception as e:
        print(f"[ERROR] ❌ 推理异常: {e}", flush=True)
        traceback.print_exc()
        sys.exit(1)

    # 输出结果
    print(f"\n{'='*40}", flush=True)
    print("[RESULT] 推理结果:", flush=True)

    if len(boxes) == 0:
        print("  未检测到人脸 / 表情", flush=True)
    else:
        for i in range(len(boxes)):
            cls_id = int(class_ids[i])
            conf = float(scores[i])
            cls_name = CLASS_NAMES[cls_id] if cls_id < len(CLASS_NAMES) else f"未知({cls_id})"
            zh_name = CLASS_ZH.get(cls_name, cls_name)
            xyxy = [round(float(v), 1) for v in boxes[i]]
            print(f"  ✅ {zh_name}({cls_name}) | 置信度: {conf:.3f} | 框: {xyxy}", flush=True)

    print("=" * 40, flush=True)

    # 保存标注图片（使用 PIL 支持中文）
    if len(boxes) > 0:
        font_path = find_chinese_font()

        for i in range(len(boxes)):
            x1, y1, x2, y2 = [int(v) for v in boxes[i]]
            cls_id = int(class_ids[i])
            conf = float(scores[i])
            cls_name = CLASS_NAMES[cls_id] if cls_id < len(CLASS_NAMES) else "?"
            zh_name = CLASS_ZH.get(cls_name, cls_name)

            cv2.rectangle(img, (x1, y1), (x2, y2), (0, 255, 0), 2)
            label = f"{zh_name} {conf:.2f}"
            img = draw_chinese_text(img, label, (x1, max(0, y1 - 25)),
                                    font_path, font_size=18, color=(0, 255, 0))

        out_dir = os.path.dirname(args.image)
        out_name = os.path.splitext(os.path.basename(args.image))[0] + "_result.jpg"
        out_path = os.path.join(out_dir, out_name)
        cv2.imwrite(out_path, img)
        print(f"[INFO] 结果图已保存: {out_path}", flush=True)


if __name__ == "__main__":
    main()
