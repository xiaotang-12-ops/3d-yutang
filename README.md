# 福建三维鱼塘 · 公开开发版

网页和 Unreal Engine 工程展示区域选择、鱼塘经营数据及双屏交互。仓库内的九块示意区域、地形和 26 条调研记录均为虚构开发样本。地图无需联网即可加载。虚构区域形状与实际行政区无关。

## 网页

安装支持 Vite 7 的 Node.js（22.12 或更新的 22.x 版本即可）。在仓库根目录运行：

```sh
npm ci
npm run check:data
npm run dev
```

打开终端显示的本机地址（默认端口 5178）。修改网页后可运行 `npm run build`。调整 `shared/fujian.geojson` 后，运行 `npm run data` 同步网页与原生工程数据，再运行检查。默认九个区域使用既有城市代码，保留 152 个模拟鱼塘。使用自己的边界时需提供相同代码、Polygon 或 MultiPolygon 及中心点，并自行取得数据分发权限。

## Windows 原生工程

主工程为 `unreal/FujianSandbox58/FujianSandbox.uproject`，数据屏为 `experiments/fujian-dashboard-motion/PondMotion.uproject`。安装 UE 5.8、Visual Studio 2022 的 C++ 游戏开发工具和 Windows SDK。两个工程都从编辑器所在机器的引擎目录获取插件。可通过引擎的 Build.bat 编译，在 PowerShell 从仓库根目录运行以下命令，将引擎路径换成本机安装位置：

```powershell
$ue = 'D:\ue5.8\UE_5.8'
& "$ue\Engine\Build\BatchFiles\Build.bat" FujianSandboxEditor Win64 Development "-Project=$(Resolve-Path unreal/FujianSandbox58/FujianSandbox.uproject)"
& "$ue\Engine\Build\BatchFiles\Build.bat" PondMotionEditor Win64 Development "-Project=$(Resolve-Path experiments/fujian-dashboard-motion/PondMotion.uproject)"
```

编译后打开对应的 .uproject。主工程常规预览载入示例地形，调研数据路径默认为 `shared/xinluo-survey.json`。数据屏在编辑器的 Session Frontend → Automation 可运行 `Xinluo.ContractAndNavigation`。改变调研记录时需按 `shared/XinluoSurveyData.h` 的字段顺序、UTF-8 字节长度和八位坐标小数计算 SHA-1 摘要。

## 地形和实验素材

主工程自带可编辑的演示网格和地形材质。重新生成时，在 Python 环境安装 `numpy pillow requests shapely`，从仓库根目录运行 `python scripts/prepare-terrain.py --demo --mesh-step 6 --project-dir unreal/FujianSandbox58`。再在该工程的 UE PythonScript commandlet 内执行 `scripts/create-terrain-materials.py` 重新导入纹理和材质。真实遥感与省域制图的准备过程见 `scripts/prepare-longyan-imagery.py`、`prepare-province-detail.py` 和 `prepare-province-textures.py`。使用自己的合法数据与运行环境。真实影像、地形缓存和原调查表未随仓库分发。

`unreal/FujianSandbox` 保留 UE 5.6 旧版源码；以该引擎重新生成地形与材质。`experiments/fujian-mountain-art-ab` 保存山体与植被实验的源码、处理脚本和植物模型；历史场景包含实际地图，需自行准备相应输入。`experiments/media-loop-isolation` 是视频循环实验，需自行提供视频。历史采集、分析和美术脚本可能引用原工作机路径，运行这些可选脚本前应核对输入与输出目录。公开样本不包含原演示录像、打包程序或原版外观复刻承诺。

植被、岩石及地表源素材来自 Poly Haven，以 CC0 发布，资产名保留于 `AssetsSource` 下的目录名。[Poly Haven 资产许可](https://polyhaven.com/license)
