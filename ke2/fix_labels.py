#!/usr/bin/env python3
"""修正 JAFFE 数据集中错误的 class_id 标注"""
import os
import sys

# 情绪代码到正确class_id的映射 (与项目 classNames 顺序一致)
emotion_map = {
    'AN': 0,  # angry 愤怒
    'DI': 1,  # disgust 厌恶
    'FE': 2,  # fear 恐惧
    'HA': 3,  # happy 开心
    'SA': 4,  # sad 悲伤
    'SU': 5,  # surprise 惊讶
    'NE': 6,  # neutral 中性
}

def main():
    dataset_dir = sys.argv[1] if len(sys.argv) > 1 else os.path.dirname(os.path.abspath(__file__))
    
    fixed_count = 0
    correct_count = 0
    errors = []
    details = []
    
    for f in sorted(os.listdir(dataset_dir)):
        if not f.endswith('.txt'):
            continue
        
        # 从文件名提取情绪代码: KA.AN1.39.txt -> AN
        parts = f.split('.')
        if len(parts) < 3:
            continue
        
        emotion_code = parts[1][:2]
        expected_id = emotion_map.get(emotion_code, -1)
        
        filepath = os.path.join(dataset_dir, f)
        with open(filepath, 'r') as fh:
            content = fh.read().strip()
        
        if not content:
            errors.append(f'{f}: 空文件')
            continue
        
        lines = content.split('\n')
        actual_id = int(lines[0].split()[0]) if lines else -1
        
        if actual_id != expected_id:
            # 修正 class_id
            new_lines = []
            for line in lines:
                parts_line = line.strip().split()
                if len(parts_line) >= 5:
                    parts_line[0] = str(expected_id)
                    new_lines.append(' '.join(parts_line))
            
            with open(filepath, 'w') as fh:
                fh.write('\n'.join(new_lines) + '\n')
            
            fixed_count += 1
            details.append(f'  FIX: {f} | {actual_id}->{expected_id} ({emotion_code})')
        else:
            correct_count += 1
    
    print(f'===== JAFFE 标注修正结果 =====')
    print(f'数据集目录: {dataset_dir}')
    print(f'正确: {correct_count}')
    print(f'已修正: {fixed_count}')
    print(f'空文件/错误: {len(errors)}')
    
    if details:
        print(f'\n修正详情:')
        for d in details:
            print(d)
    
    if errors:
        print(f'\n异常文件:')
        for e in errors:
            print(f'  {e}')

if __name__ == '__main__':
    main()
