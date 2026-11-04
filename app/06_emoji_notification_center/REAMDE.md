
## Build Command
```powershell
python .\build.py -b --app 06_emoji_notification_center --pristine -j 12
python .\build.py -f --app 06_emoji_notification_center --port COM13
python .\build.py -m --port COM13
```


## Flow Diagram
```mermaid
flowchart TD
    A["Joystick + PRG"] --> B["Input thread"]
    N["Simulated notification thread"] --> Q[("Shared k_msgq")]
    B --> Q
    Q --> D["Display thread"]
    D --> U{"UI state"}
    U --> H["Help screen"]
    U --> E["Emoji browser"]
    U --> S["Selection animation"]
    U --> F["Notification overlay"]
    H --> O["SSD1306 OLED"]
    E --> O
    S --> O
    F --> O

```