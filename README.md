# Radiance cascade for GI in unreal
A plugin that implements GI using Radiance cascades with Ray splitting

Article on how it works:
[https://www.jacktollenaar.top/articles/split-rc.html](https://www.jacktollenaar.top/articles/split-rc.html)

### World Space
Multi bounce split radiance cascades implementation
<img width="782" height="782" alt="StairRC" src="https://github.com/user-attachments/assets/34cfbb9b-b863-42b7-8588-09a28da6c8ba" />
<img width="1116" height="697" alt="SaintRC" src="https://github.com/user-attachments/assets/f3aa0a67-6661-4ac4-bfab-d4a788bf2cbd" />
<img width="1920" height="1080" alt="BedRC" src="https://github.com/user-attachments/assets/c542fd49-d96a-4655-9e00-c082958bbfd4" />

### Screen Space
A single bounce screen space version that is very fast but only works on visible lights and has quite some artifacts.
<img width="1344" height="871" alt="image" src="https://github.com/user-attachments/assets/505b430b-cadb-46c2-9fc3-c0ef6a6a09b5" />
<img width="1346" height="866" alt="image" src="https://github.com/user-attachments/assets/6d53ccdd-0ae2-4cd4-b4e7-e21825ebd340" />



## Installation
### Requirements
Plugin was made for unreal 5.8.2

Requires GPU feature level SM6

World space requires HW Raytracing support

C++ compilation for unreal ([how to build the unreal editor](https://dev.epicgames.com/community/learning/tutorials/k8Ve/unreal-engine-how-to-build-the-unreal-editor))

### Plugin
Add [RadianceCascadeGI](./Plugins/RadianceCascadeGI) to your Projects Plugins folder 

### World space
Set your Project Settings/Engine/Rendering/Global Illumination *Dynamic Global Illumination Method* to Plugin.
Go to Project Settings/Plugins/Radiance Cascades/World space
and set *Enabled* to *true*

### Screen space
To enable screen space go to Project Settings/Plugins/Radiance Cascades/Screen space
and set *Enabled* to *true*
