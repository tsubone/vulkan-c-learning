# Copilot Instructions

このリポジトリは、C言語でVulkanを段階的に学習するためのサンプル集です。

## 基本方針

- C++ではなくC言語で書く。
- GLFWではなく、可能な限りWin32 APIを使う。
- サンプルは小さく、段階的に理解できる構成にする。
- Vulkan APIの呼び出し順序を重視する。
- エラー処理は `VkResult` を確認し、失敗時は分かりやすく表示する。
- コメントは日本語で、初心者が復習しやすい説明にする。

## ビルド方針

- Windows + Visual Studio 2022 を想定する。
- Vulkan SDK がインストール済みである前提にする。
- shader は `glslc` または `glslangValidator` で SPIR-V に変換する。
