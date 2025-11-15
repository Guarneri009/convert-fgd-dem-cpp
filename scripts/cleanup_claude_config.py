import json
import os

# .claude.jsonを読み込む
config_path = '/home/gurneri009/.claude.json'
with open(config_path, 'r') as f:
    config = json.load(f)

# 存在しないプロジェクトを削除
deleted_count = 0
projects_to_keep = {}

for project_path, project_data in config.get('projects', {}).items():
    if os.path.exists(project_path):
        projects_to_keep[project_path] = project_data
    else:
        print(f"削除: {project_path}")
        deleted_count += 1

# 更新
config['projects'] = projects_to_keep

# 保存
with open(config_path, 'w') as f:
    json.dump(config, f, indent=2)

print(f"\n✅ {deleted_count}個の削除されたプロジェクト設定をクリーンアップしました")
print(f"残存プロジェクト数: {len(projects_to_keep)}")
