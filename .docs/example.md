# Example: End-to-End Pipeline Usage

> 這是一個用於測試 PR 流程的範例文件，展示 `ws_pipeline_dispatcher` 的完整使用情境。

---

## 情境說明

假設你有一個 WebSocket 伺服器，需要將不同類型的訊息（日誌、影像、感測器資料）分發給不同的下游處理器。

---

## 1. 環境準備

```bash
# 安裝依賴
pip install ws_pipeline_dispatcher

# 或從原始碼安裝
git clone https://github.com/Grasonyang/ws_pipeline_dispatcher.git
cd ws_pipeline_dispatcher
pip install -e .
```

---

## 2. 建立 Pipeline Dispatcher

```python
from ws_pipeline_dispatcher import PipelineDispatcher

# 初始化 dispatcher，監聽 ws://localhost:8765
dispatcher = PipelineDispatcher(
    host="localhost",
    port=8765,
)
```

---

## 3. 註冊處理管道

```python
# 日誌類訊息 → StreamLogger
@dispatcher.route(topic="log")
async def handle_log(message: dict):
    print(f"[LOG] {message['payload']}")

# 影像幀 → ClipStore
@dispatcher.route(topic="frame")
async def handle_frame(message: dict):
    frame_data = message['payload']
    await clip_store.append(frame_data)

# 感測器資料 → 自訂處理器
@dispatcher.route(topic="sensor")
async def handle_sensor(message: dict):
    value = message['payload']['value']
    await process_sensor(value)
```

---

## 4. 啟動服務

```python
import asyncio

async def main():
    await dispatcher.start()
    print("Pipeline Dispatcher is running...")
    await asyncio.Event().wait()  # keep alive

asyncio.run(main())
```

---

## 5. 發送測試訊息（客戶端）

```python
import asyncio
import websockets
import json

async def send_test():
    uri = "ws://localhost:8765"
    async with websockets.connect(uri) as ws:
        # 發送日誌訊息
        await ws.send(json.dumps({
            "topic": "log",
            "payload": "System started successfully."
        }))

        # 發送感測器資料
        await ws.send(json.dumps({
            "topic": "sensor",
            "payload": {"sensor_id": "temp_01", "value": 36.5}
        }))

        print("Messages sent.")

asyncio.run(send_test())
```

---

## 6. 預期輸出

```
Pipeline Dispatcher is running...
[LOG] System started successfully.
[SENSOR] temp_01 = 36.5
```

---

## 相關文件

- [Full Spec](.docs/full_spec.md)
- [Pipeline Dispatcher v1.0](.docs/pipeline_dispatcher-v1.0.md)
- [Stream Logger v1.0](.docs/stream_logger-v1.0.md)
- [Clip Store v1.0](.docs/clip_store-v1.0.md)
