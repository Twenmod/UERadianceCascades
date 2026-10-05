# Radiance cascade for GI in unreal
A plugin that implements GI using Radiance cascades following 2 different techniques


### World Space
A single bounce split radiance cascades implementation that is slower but gives world space results.
<img width="1353" height="885" alt="image" src="https://github.com/user-attachments/assets/1c1a42fd-a42f-4491-a8a7-4496a07715f0" />
<img width="1285" height="832" alt="image" src="https://github.com/user-attachments/assets/a30814f6-27b3-4544-a93f-5df2d7ff2b8c" />

### Screen Space
A single bounce screen space version that is very fast but only works on visible lights
<img width="1344" height="871" alt="image" src="https://github.com/user-attachments/assets/505b430b-cadb-46c2-9fc3-c0ef6a6a09b5" />
<img width="1346" height="866" alt="image" src="https://github.com/user-attachments/assets/6d53ccdd-0ae2-4cd4-b4e7-e21825ebd340" />



## Installation
Add [RadianceCascadeGI](./Plugins/RadianceCascadeGI) to your Projects Plugins folder 

### World space
Set your Project Settings/Engine/Rendering/Global Illumination *Dynamic Global Illumination Method* to Plugin
 go to Project Settings/Plugins/Radiance Cascades/World space
and set *Enabled* to *true*

### Screen space
To enable screen space go to Project Settings/Plugins/Radiance Cascades/Screen space
and set *Enabled* to *true*
