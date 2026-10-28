# Componentes de terceiros

## Xenos — DarthTon

Referência: https://github.com/DarthTon/Xenos, commit `4bd7399bf200f9e91f411402a3c3ac7fa4623c7e`.

O modelo de configuração em `src/core/profile.hpp` adapta os campos relevantes de `ProfileMgr::ConfigData`. A organização de validação e da inspeção de módulos segue `InjectionCore::ValidateContext` e `DlgModules::RefreshList`, com implementação Win32 própria. A interface, o motor de execução e o formato XML do Xenos não foram importados.

Copyright (c) 2013 DarthTon. [Licença MIT completa](licenses/Xenos.txt).

## Blackbone — DarthTon

Referência: https://github.com/DarthTon/Blackbone, commit `0072fba51c81aec5c6f56b7a7705377fe2f785d1`.

`vendor/blackbone/HandleGuard.hpp` deriva de `src/BlackBone/Include/HandleGuard.h`. Alterações: dependências reduzidas a Windows SDK e biblioteca padrão; validade de handles verificada contra zero e `INVALID_HANDLE_VALUE`, sem comparação de sinal. Não há link com o runtime, driver, RPC ou mapper do Blackbone.

A preservação de registradores, flags e retorno por salto indireto de Thread Hijack, em `src/core/loaders.cpp`, adapta o fluxo x64 de `Process/RPC/RemoteExec.cpp`, `RemoteExec::ExecInAnyThread`. Acrescentamos salvamento de XMM0–XMM5, alinhamento explícito da pilha, separação de páginas de código/dados e tratamento de resultado indeterminado. Nenhum código de driver foi integrado.

Copyright (c) 2015 DarthTon. [Licença MIT completa](licenses/Blackbone.txt).

## Dear ImGui e ícones

Dear ImGui mantém seus avisos nos arquivos de `vendor/imgui`. O ícone Discord tem origem e licença documentadas em [assets/icons/discord.README.md](assets/icons/discord.README.md).
