<p align="center">
  <img src="assets/images/readme-banner.svg" alt="Injector — aplicativo Windows x64 com C++20, Dear ImGui e DirectX 11" width="100%" />
</p>

<p align="center">
  <strong>Uma interface nativa para trabalhar com DLLs e processos Windows.</strong><br />
  Lista de processos, configurações visuais e logs detalhados.
</p>

<p align="center">
  <a href="#compilar"><img src="https://img.shields.io/badge/Windows-10%20%2F%2011%20%C2%B7%20x64-0078D4?style=flat-square&amp;labelColor=111827" alt="Windows 10 e 11, x64" /></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/C%2B%2B-20-00599C?style=flat-square&amp;logo=cplusplus&amp;logoColor=white&amp;labelColor=111827" alt="C++20" /></a>
  <a href="vendor/imgui"><img src="https://img.shields.io/badge/Dear%20ImGui-1.92.7-1E75FC?style=flat-square&amp;labelColor=111827" alt="Dear ImGui 1.92.7" /></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/DirectX-11-76B900?style=flat-square&amp;labelColor=111827" alt="DirectX 11" /></a>
  <a href="Injector.sln"><img src="https://img.shields.io/badge/Visual%20Studio-2022%2B-9B6DFF?style=flat-square&amp;labelColor=111827" alt="Visual Studio 2022 ou mais recente" /></a>
  <a href="CMakeLists.txt"><img src="https://img.shields.io/badge/CMake-3.20%2B-008FBA?style=flat-square&amp;logo=cmake&amp;logoColor=white&amp;labelColor=111827" alt="CMake 3.20 ou mais recente" /></a>
</p>

<p align="center">
  <a href="#recursos">Recursos</a> &nbsp;·&nbsp;
  <a href="#interface">Interface</a> &nbsp;·&nbsp;
  <a href="#tecnologias">Tecnologias</a> &nbsp;·&nbsp;
  <a href="#compilar">Compilar</a> &nbsp;·&nbsp;
  <a href="#estrutura">Estrutura</a> &nbsp;·&nbsp;
  <a href="#contato">Contato</a>
</p>

---

<a id="recursos"></a>

## ✨ O que você encontra

O **Injector** é um aplicativo desktop para carregar DLLs em processos **x64**, com interface em Dear ImGui e renderização em DirectX 11. A navegação reúne três páginas: **Injector**, **Settings** e **About**.

| Recurso | Como funciona |
| :--- | :--- |
| **Processos em tempo real** | Lista obtida do Windows, com nome, PID, arquitetura, pesquisa e atualização manual. |
| **Três métodos disponíveis** | LoadLibrary, Manual Map e Thread Hijack, selecionados nas configurações. |
| **Log de operações** | Mensagens com horário e cores por severidade, quebra de linha e histórico com rolagem. |
| **Ajustes de execução** | Delay configurável, nova tentativa automática em caso de falha e modo de log detalhado. |
| **Aparência personalizável** | Opacidade da janela, cores de destaque, animações e redução de movimento. |
| **Notificações e arquivo de log** | Alertas de sucesso e erro, efeitos sonoros opcionais e retenção configurável dos logs. |

<a id="interface"></a>

## 🖥️ Interface

<p align="center">
  <img src="preview.png" alt="Tela About do Injector, com informações do projeto, tecnologias e contato" width="900" />
</p>

<p align="center"><sub>Tema escuro, destaque em azul e painéis organizados por função.</sub></p>

<a id="tecnologias"></a>

## 🧩 Por dentro da stack

| Tecnologia | Papel no projeto |
| :--- | :--- |
| **C++20** | Código da aplicação, gerenciamento de estado e integração nativa. |
| **Dear ImGui 1.92.7** | Interface, campos de edição e controles de interação. |
| **DirectX 11 / DXGI** | Renderização da interface e apresentação dos frames. |
| **Win32 API** | Janela, diálogos de arquivos e informações dos processos. |
| **DWM / Windows Imaging Component** | Composição visual da janela e suporte às imagens. |
| **Visual Studio / MSVC** | Solução nativa para desenvolver e compilar no Windows. |
| **CMake** | Configuração alternativa de compilação. |

As dependências do Dear ImGui já estão em [`vendor/imgui`](vendor/imgui). As fontes, os ícones e as imagens utilizados pela interface ficam em [`assets`](assets), com versões embutidas no código.

<a id="compilar"></a>

## ⚙️ Do código à janela

### Requisitos

- **Windows 10 ou 11**.
- **Visual Studio 2022 ou mais recente**, com a carga de trabalho **Desenvolvimento para desktop com C++** e o Windows SDK.
- **CMake 3.20+**, caso escolha compilar por CMake.

### 1. Clone o repositório

```bash
git clone https://github.com/er1ck-xyz/Injector.git
cd Injector
```

### 2. Escolha como compilar

<details open>
<summary><strong>Visual Studio Community</strong> · solução pronta para abrir</summary>

1. Abra [`Injector.sln`](Injector.sln).
2. Selecione **Release** e **x64** na barra de ferramentas.
3. Compile com **Ctrl + Shift + B**.
4. Execute com **F5** ou abra o arquivo gerado:

```text
build/Release/Injector.exe
```

</details>

<details>
<summary><strong>CMake</strong> · compilação pelo terminal</summary>

Na pasta do projeto, execute:

```powershell
cmake -S . -B build/cmake -G "Visual Studio 17 2022" -A x64
cmake --build build/cmake --config Release
```

O executável será gerado em:

```text
build/cmake/Release/Injector.exe
```

Os comandos acima usam o gerador do Visual Studio 2022. Para outra versão instalada, escolha o gerador correspondente.

</details>

### 3. Explore o aplicativo

1. Em **Injector**, use **Search Process** para localizar e selecionar um processo x64.
2. Clique em **Browse** e escolha a DLL x64.
3. Em **Settings**, configure método, delay, notificações e aparência.
4. Volte à página **Injector**. O painel **Injection Log** mostra os resultados das operações.

<a id="estrutura"></a>

## 🗂️ Um mapa do projeto

```text
Injector/
├── Injector.sln          # Solução do Visual Studio
├── Injector.vcxproj      # Configuração do projeto C++
├── CMakeLists.txt        # Configuração do CMake
├── src/
│   ├── main.cpp          # Inicialização e loop da aplicação
│   ├── process.*         # Enumeração e consulta dos processos
│   ├── inject.*          # Métodos de carregamento, estado e logs
│   ├── ui/               # Páginas, widgets, tema e recursos embutidos
│   └── injector/         # Janela, renderização e efeitos visuais
├── assets/               # Fontes, ícones e imagens
├── vendor/imgui/         # Dear ImGui e backends utilizados
├── tests/injtest.dll     # DLL x64 para validação local
└── preview.png           # Captura da interface
```

<details>
<summary><strong>Quer personalizar a interface?</strong></summary>

| Arquivo | O que editar |
| :--- | :--- |
| [`src/ui/pages.cpp`](src/ui/pages.cpp) | Conteúdo e layout das páginas. |
| [`src/ui/widgets.cpp`](src/ui/widgets.cpp) | Aparência e comportamento dos controles. |
| [`src/ui/theme.hpp`](src/ui/theme.hpp) | Cores e constantes visuais. |
| [`src/ui/ui.hpp`](src/ui/ui.hpp) | Preferências em `ui::prefs` e estado em `ui::vars`. |

```cpp
#include "ui/ui.hpp"

ui::prefs.enable_animations = true;
ui::prefs.verbose_logging = true;
ui::vars.window_opacity = 100.f;
ui::vars.injection_delay = 150.f;
```

</details>

<a id="testes"></a>

## 🧪 DLL de teste

A [`tests/injtest.dll`](tests/injtest.dll) é uma DLL **x64** de validação. Quando executada, ela exibe a janela **`INJTEST-MARKER`**. O arquivo é independente do aplicativo e não é necessário para compilar o projeto.

<details>
<summary><strong>Ferramentas opcionais de desenvolvimento</strong></summary>

A captura de frames e a entrada de interface por script ficam desativadas nas compilações normais. Para habilitá-las em uma compilação de teste:

**MSBuild** — execute no Developer PowerShell do Visual Studio:

```powershell
msbuild Injector.sln /p:Configuration=Release /p:Platform=x64 /p:EnableTestHarness=true
```

**CMake:**

```powershell
cmake -S . -B build/cmake-tests -G "Visual Studio 17 2022" -A x64 -DINJECTOR_ENABLE_TEST_HARNESS=ON
cmake --build build/cmake-tests --config Release
```

Essas compilações aceitam `INJECTOR_CAPTURE` e `INJECTOR_CAPTURE_SCRIPT`.

</details>

<a id="contato"></a>

## 💬 Contato e feedback

Encontrou um problema? Abra uma [issue](https://github.com/er1ck-xyz/Injector/issues) com os passos para reproduzir, a mensagem do log e, quando possível, uma captura da interface.

<p align="center">
  <a href="https://github.com/er1ck-xyz/Injector"><img src="https://img.shields.io/badge/GitHub-er1ck--xyz-1E75FC?style=for-the-badge&amp;logo=github&amp;logoColor=white&amp;labelColor=111827" alt="GitHub de er1ck-xyz" /></a>
  <a href="https://github.com/er1ck-xyz/Injector/issues"><img src="https://img.shields.io/badge/Feedback-Abrir%20issue-8B5CF6?style=for-the-badge&amp;labelColor=111827" alt="Abrir uma issue no repositório" /></a>
  <img src="https://img.shields.io/badge/Discord-.0x764-5865F2?style=for-the-badge&amp;logo=discord&amp;logoColor=white&amp;labelColor=111827" alt="Discord: .0x764" />
</p>

---

<p align="center">
  <sub>Desenvolvido por <strong>I am That I Am</strong> · <a href="#recursos">Voltar ao início ↑</a></sub>
</p>
