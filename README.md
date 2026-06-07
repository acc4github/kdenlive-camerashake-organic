# Camera Shake Organic for Kdenlive
This is a modified Frei0r plugin for Kdenlive that provides more organic and irregular camera shake with noise-based movement on position, rotation, and blur.

<p align="center">
<img width="444" height="359" alt="kdenlive_13LMMaXrVi" src="https://github.com/user-attachments/assets/268d7295-9995-403a-9228-ebf23756e3e1" />
</p>

| Original Camera Shake Ultimate | Camera Shake Organic (Modified) |
|--------------------------------|---------------------------------|
| <video src="https://github.com/user-attachments/assets/5814dd0c-9cf5-4317-a0e0-af4dfee853ab" controls width="400"></video> | <video src="https://github.com/user-attachments/assets/90107a4b-d144-49dc-8537-f3db214c77fe" controls width="400"></video> |

---
### Original Plugin
- Author: PakeMPC
- Original repository: https://github.com/PakeMPC/kdenlive-camerashake
- Original license: GNU GPLv3

### Modifications
- Made by: acc4commissions
- AI Assistance: Grok 4.3 (xAI)
- Changes:
  - Much more random/noisy movement overall
  - Added fluctuation to blur (controlled by the speed, 55% rate) to emulate the camera's focal point miss
  - Much smoother visuals with Bilinear Interpolation
  - Reduced overall intensity of the sliders for microadjustments
  - Added 'Seed' parameter to give variations between clips
  - Removed opacity parameter
  - Removed background color
  - Different plugin name to avoid conflict (`camerashakeorganic.dll`)

### Important Warning
**This is NOT an official Kdenlive plugin.**  
This is a modified third-party plugin.  
- It may stop working after Kdenlive or Frei0r updates.
- Not tested on all systems or versions.
- Use at your own risk.

### Installation (Windows)
1. Download the build from the release. The zip file should have `camerashakeorganic.dll` and `camerashakeorganic.xml`.
2. Place `camerashakeorganic.dll` in Kdenlive frei0r plugins folder (e.g., kdenlive-master\lib\frei0r-1)
3. Place `camerashakeorganic.xml` in Kdenlive effects folder (e.g., kdenlive-master\bin\data\kdenlive\effects)
4. Restart Kdenlive

### Building from Source
See `CMakeLists.txt` and original repo instructions. I don't even know how I managed to build this shit.

### License
This project is licensed under the same GNU General Public License v3.0 as the original.  
See LICENSE file for details.
You are free to use, modify, and distribute as long as you follow GPLv3 terms (provide source code, keep license, etc.).
