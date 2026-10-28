<p align="center">
  <img src="assets/images/readme-banner.svg" alt="Injector — aplicativo Windows x64 com C++20, Dear ImGui e DirectX 11" width="100%" />
</p>

<p align="center">
  Injetor de DLLs para Windows x64, escrito em C++20 com Dear ImGui e DirectX 11.
</p>

<p align="center">
  <a href="#compilar"><img src="https://img.shields.io/badge/Windows-10%20x64-0078D4?style=flat-square&amp;labelColor=111827" alt="Windows 10 x64, validado na build 19045" /></a>
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

## Recursos

- LoadLibrary, Manual Map e Thread Hijack.
- Lista de processos com nome, PID, arquitetura, pesquisa e atualização manual.
- Logs com horário, cores por severidade, quebra de linha e rolagem.
- Delay configurável. Retry automático bloqueado após resultado indeterminado.
- Inspeção dos módulos carregados e validação PE antes da execução.
- Restauração automática das configurações ao abrir.
- Opacidade, cores e animações ajustáveis.
- Notificações, sons opcionais e gravação de logs em arquivo.

<a id="interface"></a>

## Interface

<p align="center">
  <img src="preview.png" alt="Tela About do Injector, com informações do projeto, tecnologias e contato" width="900" />
</p>

<a id="tecnologias"></a>

## Tecnologias

| Tecnologia | Uso |
| :--- | :--- |
| **C++20** | Linguagem. |
| **Dear ImGui 1.92.7** | Interface. |
| **DirectX 11 / DXGI** | Renderização. |
| **Win32 API** | Janela, diálogos e consulta de processos. |
| **DWM / Windows Imaging Component** | Composição da janela e imagens. |
| **Visual Studio / MSVC** | IDE e compilador. |
| **CMake** | Configuração da compilação. |

Dear ImGui está incluído em [`vendor/imgui`](vendor/imgui). Fontes, ícones e imagens ficam em [`assets`](assets) e são embutidos no executável.

<a id="compilar"></a>

## Compilação

### Requisitos

- **Windows 10 x64** (testado na build 19045). Windows 11 ainda não foi validado.
- **Visual Studio 2022 ou mais recente**, com a carga de trabalho **Desenvolvimento para desktop com C++** e o Windows SDK.
- **CMake 3.20+** para compilar pelo CMake.

### Clonar

```bash
git clone https://github.com/er1ck-xyz/Injector.git
cd Injector
```

<details open>
<summary><strong>Visual Studio Community</strong></summary>

1. Abra [`Injector.sln`](Injector.sln).
2. Selecione **Release** e **x64** na barra de ferramentas.
3. Compile com **Ctrl + Shift + B**.
4. Execute com **F5** ou abra o arquivo gerado:

```text
build/Release/Injector.exe
```

</details>

<details>
<summary><strong>CMake</strong></summary>

Na pasta do projeto:

```powershell
cmake -S . -B build/cmake -G "Visual Studio 17 2022" -A x64
cmake --build build/cmake --config Release
```

Executável:

```text
build/cmake/Release/Injector.exe
```

Para outra versão do Visual Studio, ajuste o gerador em `-G`.

</details>

## Uso

1. Selecione um processo x64 na lista.
2. Clique em **Browse** e selecione uma DLL x64.
3. Configure o método e o delay em **Settings**.
4. Clique em **Inject** e acompanhe o resultado em **Injection Log**.

<a id="estrutura"></a>

## Estrutura

```text
Injector/
├── Injector.sln          # Solução do Visual Studio
├── Injector.vcxproj      # Configuração do projeto C++
├── CMakeLists.txt        # Configuração do CMake
├── src/
│   ├── main.cpp          # Inicialização e loop da aplicação
│   ├── process.*         # Enumeração e consulta dos processos
│   ├── inject.*          # Métodos de carregamento, estado e logs
│   ├── core/             # Validação PE, módulos, perfis, backends e logs
│   ├── ui/               # Páginas, widgets, tema e recursos embutidos
│   └── injector/         # Janela, renderização e efeitos visuais
├── assets/               # Fontes, ícones e imagens
├── vendor/               # Dear ImGui e HandleGuard do Blackbone
├── tests/                # DLL de teste, fixtures e regressões do domínio
├── docs/                 # Arquitetura, migração e resultados dos testes
└── preview.png           # Captura da interface
```

<details>
<summary><strong>Personalização</strong></summary>

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

## DLL de teste

[`tests/injtest.dll`](tests/injtest.dll) é uma DLL x64 que exibe a janela `INJTEST-MARKER` quando executada. Não é necessária para compilar o projeto.

<details>
<summary><strong>Ferramentas de teste</strong></summary>

Captura de frames e entrada por script são desativadas por padrão. Para habilitar:

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

## Configurações e operações

As configurações são salvas automaticamente ao fechar o programa e restauradas ao abrir. Elas e o log ficam em `%LOCALAPPDATA%/Injector/`. PIDs não são persistidos: selecione o processo a cada sessão.

**Modules** lista nome, base, tamanho e caminho dos módulos do loader. Imagens carregadas por Manual Map não aparecem nessa lista. **Cancel** cancela uma operação ainda não executada ou encerra a espera local quando a execução já começou.

Um timeout após início da execução tem resultado **Indeterminate**. A DLL pode terminar de carregar depois. O programa mantém os buffers ainda em uso no alvo e bloqueia retry automático.

Manual Map suporta DLLs nativas x64 com imports, relocations e tabelas de unwind. TLS, delay imports, load configuration, CLR e resolução completa de API sets/SxS não estão implementados. Thread Hijack foi testado no processo de teste incluído; AVX/XSTATE e processos com CET não foram validados. Consulte o [relatório de compatibilidade e testes](docs/refactoring.md).

## Testes

Os testes são opcionais e usam processos criados pelo próprio harness:

```powershell
cmake -S . -B build/tests -G "Visual Studio 17 2022" -A x64 -DINJECTOR_BUILD_TESTS=ON
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

A análise do Xenos, as decisões de integração e os resultados ficam em [docs/refactoring.md](docs/refactoring.md). Os componentes adaptados de Xenos/Blackbone e suas licenças estão em [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).

<a id="contato"></a>

## Contato

[Issues](https://github.com/er1ck-xyz/Injector/issues) · Discord: **`.0x764`**

Ao reportar um erro, inclua os passos para reproduzir e a mensagem do log.

<p align="center">
  <a href="https://github.com/er1ck-xyz/Injector"><img src="https://img.shields.io/badge/GitHub-er1ck--xyz-1E75FC?style=for-the-badge&amp;logo=github&amp;logoColor=white&amp;labelColor=111827" alt="GitHub de er1ck-xyz" /></a>
  <a href="https://github.com/er1ck-xyz/Injector/issues"><img src="https://img.shields.io/badge/Feedback-Abrir%20issue-8B5CF6?style=for-the-badge&amp;labelColor=111827" alt="Abrir uma issue no repositório" /></a>
  <img src="https://img.shields.io/badge/Discord-.0x764-5865F2?style=for-the-badge&amp;logo=discord&amp;logoColor=white&amp;labelColor=111827" alt="Discord: .0x764" />
</p>

---

<p align="center">
  <sub>Desenvolvido por <strong>I am That I Am</strong> · <a href="#recursos">Voltar ao início ↑</a></sub>
</p>
