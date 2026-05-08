# Vulkan C Learning

![Build](https://github.com/tsubone/vulkan-c-learning/actions/workflows/build.yml/badge.svg)

C言語で Vulkan を学習するためのリポジトリです。  
Windows / Visual Studio 2022 / Win32 API を使って、Vulkan の基本的な初期化から三角形描画、毎フレーム描画ループ、バッファ、Uniform Buffer、Descriptor などを段階的に学んでいきます。

## 目的

このリポジトリの目的は、Vulkan の仕組みを C言語で一つずつ理解することです。

特に以下を重視します。

- GLFW などのライブラリに頼りすぎず、Win32 API でウィンドウを作る
- Vulkan の各オブジェクトの役割を理解する
- サンプルコードを小さな段階に分けて学ぶ
- Visual Studio 2022 でビルドできる形にする
- 「なぜこの API が必要なのか」を確認しながら進める

## 開発環境

現在の想定環境です。

| 項目 | 内容 |
|---|---|
| OS | Windows |
| IDE | Visual Studio 2022 |
| 言語 | C |
| Graphics API | Vulkan |
| Window API | Win32 API |
| Vulkan SDK | LunarG Vulkan SDK |
| Shader Compiler | glslc / glslangValidator |

## 必要なもの

事前に以下をインストールしておきます。

- Visual Studio 2022
- Vulkan SDK
- C/C++ 開発ツール
- Git
- GitHub アカウント

Vulkan SDK をインストールすると、通常は以下のような環境変数が設定されます。

```text
VULKAN_SDK