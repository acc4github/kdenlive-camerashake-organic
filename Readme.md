# Camera Shake Organic for Kdenlive
Modified version of Camera Shake Ultimate

This is a modified Frei0r plugin for Kdenlive that provides more organic and irregular camera shake with noise-based movement on position, rotation, and blur.

### Original Plugin
- Author: PakeMPC
- Original repository: https://github.com/PakeMPC/kdenlive-camerashake
- Original license: GNU GPLv3

### Modifications
- Made by: acc4commissions
- Assistance: Code rewrite generated with help from Grok 4.3 (xAI)
- Changes:
  - Much more random/noisy movement on position, rotation, and blur
  - Blur stays mostly near zero with rare short spikes to emulate camera's focal point miss
  - Removed opacity parameter
  - Removed background color
  - Different plugin name to avoid conflict (`camerashakeorganic.dll`)

### Important Warning
**This is NOT an official Kdenlive plugin.**  
It is a third-party modified version.  
- It may stop working after Kdenlive or Frei0r updates.
- Not tested on all systems or versions.  
- Use at your own risk.  

### Installation
1. Download `camerashakeorganic.dll`
2. Place it in your frei0r plugins folder (e.g., kdenlive-master\lib\frei0r-1)
3. Place `camerashakeorganic.xml` in Kdenlive effects folder (e.g., kdenlive-master\bin\data\kdenlive\effects)
4. Restart Kdenlive

### Building from Source
See `CMakeLists.txt` and original repo instructions.

### License
This project is licensed under the same GNU General Public License v3.0 as the original.  
See LICENSE file for details.
You are free to use, modify, and distribute as long as you follow GPLv3 terms (provide source code, keep license, etc.).