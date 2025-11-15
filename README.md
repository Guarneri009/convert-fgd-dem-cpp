# terrain-data-generator

./build-cpu/lem2geotiff_cpp -d "../lem2geotiff/python_code/dem" -n -9999


# WSL2 Docker GPU対応エラーの解決手順

## エラー内容
```
docker: Error response from daemon: could not select device driver "" with capabilities: [[gpu]]
```

## 原因
**nvidia-container-toolkit** が正しくインストール・設定されていないことが原因です。

## 解決手順

### 1. nvidia-container-toolkitをインストール
```bash
# リポジトリの追加
distribution=$(. /etc/os-release;echo $ID$VERSION_ID)
curl -fsSL https://nvidia.github.io/libnvidia-container/gpgkey | sudo gpg --dearmor -o /usr/share/keyrings/nvidia-container-toolkit-keyring.gpg

curl -s -L https://nvidia.github.io/libnvidia-container/$distribution/libnvidia-container.list | \
    sed 's#deb https://#deb [signed-by=/usr/share/keyrings/nvidia-container-toolkit-keyring.gpg] https://#g' | \
    sudo tee /etc/apt/sources.list.d/nvidia-container-toolkit.list

# インストール
sudo apt-get update
sudo apt-get install -y nvidia-container-toolkit
```

### 2. Dockerデーモンを設定
```bash
sudo nvidia-ctk runtime configure --runtime=docker
```

このコマンドが `/etc/docker/daemon.json` を自動更新します。

### 3. Dockerを再起動
```bash
sudo systemctl restart docker
```

または、systemdが使えない場合：
```bash
sudo service docker restart
```

### 4. 動作確認
```bash
docker run --rm --gpus all nvidia/cuda:11.8.0-base-ubuntu22.04 nvidia-smi
```

## WSL2特有の注意点

WSL2では `systemctl` が動かない場合があります。その場合：
```bash
# Dockerデーモンを手動で再起動
sudo service docker stop
sudo service docker start
```

または、WSLを再起動：
```powershell
# PowerShellで実行
wsl --shutdown
# その後、WSL2を再度起動
```

## 前提条件の確認

問題が解決しない場合は、以下も確認してください：

### Windows側のNVIDIA Driverが正しくインストールされているか
```powershell
# PowerShellで確認
nvidia-smi
```

### WSL2でGPUが認識されているか
```bash
# WSL2内で確認
nvidia-smi
```

認識されていない場合：
- Windows側のNVIDIA Driverを更新（バージョン470以上が必要）
- WSLカーネルを更新：`wsl --update`