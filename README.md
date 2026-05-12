# libshadow.so — GTA SA Android Shadow Mod

## Target
- Game: com.sampmobilerp.game
- ABI: armeabi-v7a (ARM Thumb2 32-bit)
- Loader: AML (Android Mod Loader)

## Offsets (libGTASA.so)
| Fungsi | Offset |
|---|---|
| RenderExtraPlayerShadows | 0x5BDAC4 |
| RenderStoredShadows | 0x5BA720 |
| DoShadowThisFrame | 0x5B86B4 |
| RTShadowManager::Update | 0x5B83FC |
| gpShadowPedTex | 0xA48244 |
| g_realTimeShadowMan | 0xA4816C |
| bRenderShadows | 0xA46D3C |

## Install
Taruh `libshadow.so` di:
`/storage/emulated/0/Android/data/com.sampmobilerp.game/mods/`
