A simple Android frame rate and load recorder

食用方法:
``` shell
BMonitor [-w] [-t <时间>] [包名]
```
- -w: 等待对应包名的应用进入前台再开始
- -t: 录制时长,单位s, 缺省值30
例如:
``` shell
./BMonitor -w -t 60 com.miHoYo.hkrpg
```
输出:
- svg: 图表
- json: 数据，可以通过-i指令对指定json文件绘制图表

<img src="https://github.com/GlowingBrick/BLoadRecorder/raw/refs/heads/master/test_data/monitor_test.svg" width="160" height="160" style="display: block; margin: 0 auto;" alt="SVG Image">

