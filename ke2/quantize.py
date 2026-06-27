#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
YOLOv8 模型量化脚本（.pt → 量化 ONNX）
用法:
  python quantize.py --input <model.pt>
                     [--output <output.onnx>]
                     [--type FP16|INT8]

量化类型说明：
  FP16  — 半精度浮点：FP32 → FP16，体积减半 + 精度降低
  INT8  — 权重量化：FP32 → INT8(256级) → FP32，精度降低（模型体积不变）
"""

import sys
import os
import argparse
import shutil
import tempfile
import numpy as np


# ---- 工作函数 ----

def do_quantize_fp16(input_path, output_path):
    """FP16 半精度量化：ultralytics half=True 导出"""
    from ultralytics import YOLO

    tmp_dir = tempfile.mkdtemp(prefix="ke2_quant_")
    tmp_pt = os.path.join(tmp_dir, "best.pt")
    shutil.copy2(input_path, tmp_pt)

    # FP32 导出（作为对比基准）
    print("[INFO] 正在导出 FP32 ONNX（作为基准）...", flush=True)
    model1 = YOLO(tmp_pt)
    fp32_path = model1.export(format="onnx", imgsz=640, opset=12, simplify=True, half=False)
    fp32_size = os.path.getsize(fp32_path) / (1024*1024)

    # FP16 导出（真正的量化输出）
    print("[INFO] 正在导出 FP16 ONNX（量化输出）...", flush=True)
    model2 = YOLO(tmp_pt)
    fp16_path = model2.export(format="onnx", imgsz=640, opset=12, simplify=True, half=True)
    fp16_size = os.path.getsize(fp16_path) / (1024*1024)

    # 保存量化模型
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    shutil.copy2(fp16_path, output_path)

    # 同时保存 FP32 基准
    model_dir = os.path.dirname(output_path)
    fp32_save = os.path.join(model_dir, "best.onnx")
    shutil.copy2(fp32_path, fp32_save)

    # 汇总
    ratio = (1 - fp16_size/fp32_size) * 100
    print(f"\n[DONE] ✅ FP16 量化完成!")
    print(f"[INFO] {'='*50}")
    print(f"[INFO] 量化类型:      FP32 → FP16（半精度浮点）")
    print(f"[INFO] FP32 基准:     {fp32_size:.2f} MB  →  best.onnx")
    print(f"[INFO] FP16 量化:     {fp16_size:.2f} MB  →  best_quantized.onnx")
    print(f"[INFO] 体积压缩:      {ratio:.1f}%")
    print(f"[INFO] 尾数位:        FP32 23位 → FP16 10位，精度降低")
    print(f"[INFO] {'='*50}")

    shutil.rmtree(tmp_dir, ignore_errors=True)
    return output_path


def do_quantize_int8(input_path, output_path):
    """INT8 权重量化：Numpy FP32→INT8(256级)→FP32，精度降低但模型体积不变"""
    from ultralytics import YOLO
    import onnx
    from onnx import numpy_helper, helper as onnx_helper

    tmp_dir = tempfile.mkdtemp(prefix="ke2_quant_")
    tmp_pt = os.path.join(tmp_dir, "best.pt")
    shutil.copy2(input_path, tmp_pt)

    # Step 1: 导出 FP32 ONNX
    print("[INFO] 正在导出 FP32 ONNX...", flush=True)
    model = YOLO(tmp_pt)
    onnx_path = model.export(format="onnx", imgsz=640, opset=12, simplify=True, half=False)
    onnx_size_before = os.path.getsize(onnx_path) / (1024*1024)
    print(f"[INFO] FP32 ONNX 导出完成 ({onnx_size_before:.2f} MB)", flush=True)

    # Step 2: 加载 ONNX，对所有权重做 INT8 量化-反量化
    print("[INFO] 正在执行 INT8 权重量化（FP32 → INT8 → FP32）...", flush=True)
    onnx_model = onnx.load(onnx_path)

    quantized_count = 0
    total_weights = 0
    total_error = 0.0
    max_error = 0.0

    for init in onnx_model.graph.initializer:
        if init.data_type != 1:  # 1 = FLOAT
            continue
        total_weights += 1

        arr = numpy_helper.to_array(init).astype(np.float32)
        if arr.size < 100:
            continue  # 太小的张量跳过（如 bias、gamma）

        # 对称量化：找到 scale，映射到 [-127, 127]
        abs_max = np.max(np.abs(arr))
        if abs_max < 1e-8:
            continue

        scale = abs_max / 127.0
        arr_int = np.clip(np.round(arr / scale), -127, 127).astype(np.int8)
        arr_dequant = arr_int.astype(np.float32) * scale

        diff = np.abs(arr - arr_dequant)
        total_error += np.mean(diff)
        if np.max(diff) > max_error:
            max_error = np.max(diff)

        # 替换回模型
        new_init = numpy_helper.from_array(arr_dequant, name=init.name)
        onnx_model.graph.initializer.remove(init)
        onnx_model.graph.initializer.append(new_init)
        quantized_count += 1

    # 保存量化后的 ONNX
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    onnx.save(onnx_model, output_path)
    onnx_size_after = os.path.getsize(output_path) / (1024*1024)

    # 同时保存原始 FP32 作为对比基准
    model_dir = os.path.dirname(output_path)
    fp32_save = os.path.join(model_dir, "best.onnx")
    shutil.copy2(onnx_path, fp32_save)

    # 汇总
    avg_error = total_error / max(quantized_count, 1)
    pct_quantized = quantized_count / max(total_weights, 1) * 100
    print(f"\n[DONE] ✅ INT8 量化完成!")
    print(f"[INFO] {'='*50}")
    print(f"[INFO] 量化类型:      FP32 → INT8(256级) → FP32")
    print(f"[INFO] 量化权重:      {quantized_count}/{total_weights} ({pct_quantized:.1f}%)")
    print(f"[INFO] 平均误差:      {avg_error:.6f}")
    print(f"[INFO] 最大误差:      {max_error:.6f}")
    print(f"[INFO] FP32 基准:     {onnx_size_before:.2f} MB  →  best.onnx")
    print(f"[INFO] INT8 量化:     {onnx_size_after:.2f} MB  →  best_quantized.onnx")
    print(f"[INFO] 体积变化:      不变（权重仍以 FP32 存储，值已量化）")
    print(f"[INFO] 精度变化:      引入 256 级量化舍入误差，精度降低")
    print(f"[INFO] {'='*50}")

    shutil.rmtree(tmp_dir, ignore_errors=True)
    return output_path


# ---- 主入口 ----

def do_quantize(args):
    input_path = os.path.abspath(args.input)
    if not os.path.exists(input_path):
        print(f"[ERROR] 输入模型不存在: {input_path}", flush=True)
        sys.exit(1)

    if not input_path.endswith(".pt"):
        print(f"[ERROR] 量化模块需要输入训练后的 .pt 模型文件", flush=True)
        sys.exit(1)

    print(f"[INFO] 输入模型: {input_path}", flush=True)
    quant_type = getattr(args, 'type', 'FP16') or 'FP16'
    print(f"[INFO] 量化类型: {quant_type}", flush=True)
    print("PROGRESS:5%", flush=True)

    # 确定输出路径
    if args.output:
        output_path = os.path.abspath(args.output)
    else:
        model_dir = os.path.dirname(input_path)
        output_path = os.path.join(model_dir, "best_quantized.onnx")

    print("PROGRESS:10%", flush=True)

    if quant_type in ('INT8', 'QInt8', 'QUInt8'):
        print("[INFO] 使用 INT8 模式：权重经 256 级量化后还原为 FP32", flush=True)
        print("PROGRESS:20%", flush=True)
        do_quantize_int8(input_path, output_path)
    else:
        # 默认 FP16
        print("[INFO] 使用 FP16 模式：ultralytics half=True 导出", flush=True)
        print("PROGRESS:20%", flush=True)
        do_quantize_fp16(input_path, output_path)

    print("PROGRESS:100%", flush=True)
    print("QUANT_FINISHED", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="YOLOv8 模型量化（.pt → 量化 ONNX）"
    )
    parser.add_argument('--input', '-i', type=str, required=True,
                        help='输入模型路径 (.pt)')
    parser.add_argument('--output', '-o', type=str, default=None,
                        help='输出量化模型路径 (默认: 同目录 best_quantized.onnx)')
    parser.add_argument('--type', type=str, default='FP16',
                        choices=['FP16', 'INT8', 'QInt8', 'QUInt8'],
                        help='量化类型: FP16=半精度, INT8=权重量化')

    args = parser.parse_args()
    do_quantize(args)
