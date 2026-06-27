#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YOLOv8 PyTorch → ONNX 转换脚本
用法:
  python convert_onnx.py --input <model.pt> [--output <model.onnx>] [--imgsz 640]
"""

import os
import sys
import shutil
import argparse

from ultralytics import YOLO


def main():
    parser = argparse.ArgumentParser(description="YOLOv8 .pt → .onnx 转换")
    parser.add_argument('--input', '-i', type=str, required=True,
                        help='YOLOv8 PyTorch 模型路径 (.pt)')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='输出ONNX路径 (默认: 与输入同目录，自动命名为同名的 .onnx)')
    parser.add_argument('--imgsz', type=int, default=640,
                        help='输入图像尺寸 (默认: 640)')
    parser.add_argument('--opset', type=int, default=12,
                        help='ONNX opset 版本 (默认: 12)')

    args = parser.parse_args()

    input_path = os.path.abspath(args.input)
    if not os.path.exists(input_path):
        print(f"[ERROR] 输入模型不存在: {input_path}", flush=True)
        sys.exit(1)

    if args.output:
        output_path = os.path.abspath(args.output)
    else:
        output_path = os.path.splitext(input_path)[0] + ".onnx"

    out_dir = os.path.dirname(output_path)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    try:
        print("PROGRESS:10%", flush=True)
        print(f"[INFO] 加载 YOLOv8 模型: {input_path}", flush=True)

        model = YOLO(input_path)
        print(f"[INFO] 任务类型: {model.task}, 类别数: {len(model.names)}", flush=True)
        print("PROGRESS:30%", flush=True)

        print(f"[INFO] 导出 ONNX (imgsz={args.imgsz}, opset={args.opset})...", flush=True)
        print("PROGRESS:50%", flush=True)

        # YOLOv8 内部导出，默认输出到输入文件同目录
        exported_path = model.export(
            format="onnx",
            imgsz=args.imgsz,
            opset=args.opset,
            simplify=True,
        )

        print("PROGRESS:90%", flush=True)

        # export 返回的是实际生成的路径；如果需要重命名则移动
        if exported_path and os.path.exists(exported_path):
            if os.path.abspath(exported_path) != os.path.abspath(output_path):
                shutil.move(exported_path, output_path)
            print("PROGRESS:95%", flush=True)

        if os.path.exists(output_path):
            size_mb = os.path.getsize(output_path) / (1024 * 1024)
            print(f"\n[DONE] ✅ ONNX 转换成功!", flush=True)
            print(f"  输出路径: {output_path}", flush=True)
            print(f"  模型大小: {size_mb:.2f} MB", flush=True)
            print("PROGRESS:100%", flush=True)
        else:
            print("[ERROR] ONNX 文件未生成，请检查日志", flush=True)
            sys.exit(1)

    except Exception as e:
        print(f"[ERROR] ❌ 转换失败: {str(e)}", flush=True)
        import traceback
        traceback.print_exc()
        sys.exit(1)


if __name__ == "__main__":
    main()
