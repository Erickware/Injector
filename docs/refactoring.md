# Refatoração — Xenos / Blackbone

## Referências e checkpoint

- Injector antes das alterações: `6a880b0be01d4a36e7b56242b1f6fbaae1da64cd`.
- Checkpoint Git: `checkpoint/pre-xenos-refactor-20261028`.
- Backup: `build/checkpoints/pre-xenos-refactor-20261028.zip` (arquivos versionados).
- Xenos local: `4bd7399bf200f9e91f411402a3c3ac7fa4623c7e`.
- Blackbone fixado pelo Xenos: `0072fba51c81aec5c6f56b7a7705377fe2f785d1`.
- O submódulo local estava vazio. A revisão fixada foi consultada numa cópia separada em `C:/temp/Injector-Blackbone-reference`, sem alterar o Xenos local.

## Análise e decisões antes da implementação

| Xenos / Blackbone | Injector anterior | Decisão |
| --- | --- | --- |
| `InjectionCore`, `InjectContext`, `ValidateContext` | Motor, validação PE e logs em `inject.cpp`; arquitetura recebida da interface | Separar validação, backends e controle da operação; consultar o handle real do alvo |
| `Process`, `ProcessCore`, `ProcessModules` | Toolhelp e consultas documentadas; lista devolvida por referência sem proteção | Preservar Win32, publicar snapshots por valor e atualizar fora da renderização |
| `PEImage` | Verificava somente assinatura, Machine e flag DLL | Inspeção independente com limites de arquivo, seções, diretórios e compatibilidade |
| `DlgModules::RefreshList` | Nenhum inspetor público | Lista de módulos do loader com nome, base, tamanho e caminho; sem varredura de memória ou unload |
| `ProfileMgr::ConfigData`, `Save`, `Load` | Preferências só em memória | Adaptar modelo de perfil; formato próprio versionado, sem RapidXML e sem PID persistido |
| `Include/HandleGuard.h` | `CloseHandle` manual em muitos retornos | Integrar componente isolado, reduzir includes e corrigir predicado de validade |
| `Log.h` | Ring buffer e log em arquivo já existentes | Preservar ring buffer e extrair diagnóstico; corrigir concorrência no arquivo |
| `MMap`, RPC, `NtLoader`, `NameResolve`, símbolos | Manual Map limitado e stub x64; Thread Hijack x64 | Preservar métodos; adaptar a preservação de estado de `RemoteExec::ExecInAnyThread`, sem importar RPC/AsmJit |
| Driver, HideVAD, unlink, wipe headers, promoção de handles | Ausentes | Não integrar |

O Xenos usa controles Win32, RapidXML, Blackbone e interfaces de CLR. O Blackbone fixado depende de AsmJit, wow64ext, código assembler, símbolos/DIA, estruturas NT e componentes de driver. `Process` chama `InitializeOnce`, que concede privilégios, carrega símbolos e inicializa resolução de nomes. O projeto antigo usa v142/v141_xp; o Xenos também desativa ASLR. Nenhuma dessas configurações deve ser transportada para o Injector.

Problemas encontrados: worker destacado acessando estado global no encerramento; memória remota liberada após timeout; nova tentativa automática após resultado indeterminado; validação PE sem limites das seções; leitura fora do buffer no stub de Thread Hijack (`0xA0` bytes a partir de array `0x70`); cálculo inválido dos callbacks TLS; stub de tamanho fixo incompatível com muitos callbacks; operações de proteção sem conferir resultado; DLLs gerenciadas e recursos não implementados aceitos sem diagnóstico; caminhos UTF-8 abertos por APIs ANSI; mutex que não protegia referências publicadas da lista; atualização síncrona na renderização.

## Plano por componentes

1. Isolar utilitários Win32, erros, UTF-8 e handles; incluir licenças e origem.
2. Extrair inspeção PE e enumeração de módulos; compartilhar arquitetura real do processo.
3. Extrair diagnóstico e backends; corrigir ciclo de vida, timeout e resultados indeterminados.
4. Adicionar perfis e inspeção de módulos na interface, mantendo layout e controles existentes.
5. Criar biblioteca de domínio no CMake e testes opt-in com processos e DLLs próprios.
6. Compilar Release/Debug, executar testes, registrar resultados e limites separadamente.

## Limites de compatibilidade

A aplicação e os backends continuam x64. A inspeção aceita PE32/PE32+ e a lista identifica x86/x64/ARM64; carregamento cruzado não é anunciado. O backend x64 usa `CONTEXT.Rip` e código de máquina x64. Criar uma configuração Win32 sem outro backend produziria compatibilidade falsa.

Mecanismos do Windows não são desativados. Erros de permissão e políticas de mitigação devem aparecer no diagnóstico. Um timeout após início de execução remota tem resultado indeterminado: não permite retry automático nem libera buffers possivelmente em uso. Eles permanecem no alvo até seu encerramento. Cancelamento encerra a espera local; não encerra arbitrariamente uma thread remota.

Referências Win32: [Toolhelp](https://learn.microsoft.com/en-us/windows/win32/api/tlhelp32/nf-tlhelp32-createtoolhelp32snapshot), [IsWow64Process2](https://learn.microsoft.com/en-us/windows/win32/api/wow64apiset/nf-wow64apiset-iswow64process2), [PE](https://learn.microsoft.com/en-us/windows/win32/debug/pe-format), [mitigações](https://learn.microsoft.com/en-us/windows/win32/api/processthreadsapi/nf-processthreadsapi-getprocessmitigationpolicy).

## Arquitetura entregue

```text
ui (ImGui + diálogos de arquivo)
  ├── process: snapshots publicados por valor; atualização em jthread
  ├── modules: consulta documentada dos módulos do loader
  ├── profile: configuração validada, sem estado de execução persistido
  └── inject: uma operação por vez; validação, estado, cancelamento e conclusão
        ├── pe_image: análise de arquivo e compatibilidade
        ├── loaders: LoadLibrary / Manual Map / Thread Hijack
        ├── diagnostics: ring buffer e log em arquivo serializado
        └── win32: UTF-8, erros, handles e memória remota
```

O domínio não inclui ImGui, DirectX ou tipos da interface. No CMake, compila como `InjectorCore`, separado do executável e reutilizado pelos testes. A solução existente do Visual Studio continua compilando todos os módulos no executável.

A configuração atual fica em `%LOCALAPPDATA%/Injector/settings.ini`, com gravação por arquivo temporário e substituição. O armazenamento automático usa formato próprio, versão 1, sem compatibilidade com `.xpr` do Xenos. Não há exportação ou importação de perfis na interface. Um perfil inválido não altera as configurações em memória. Não se restaura PID ou inicia injeção automaticamente ao abrir um perfil.

Os logs ficam em `%LOCALAPPDATA%/Injector/injector.log`. Foi preservada a retenção por dias e o limite de 240 entradas em memória; mensagens passaram de 320 para 2048 bytes. Acesso ao arquivo é serializado e falha de abertura aparece no buffer da interface.

A seleção de processo inclui PID e data de criação. A operação mantém o mesmo handle do início ao fim e verifica essa identidade antes da execução. A arquitetura é consultada no processo, sem confiar no texto da interface. A inspeção de módulos também confere a identidade do alvo.

## Código reaproveitado e código próprio

| Origem | Integração efetiva |
| --- | --- |
| Blackbone `Include/HandleGuard.h` | Classe RAII compilada em `vendor/blackbone/HandleGuard.hpp`, com dependências mínimas e predicado corrigido |
| Blackbone `RemoteExec::ExecInAnyThread` | Fluxo x64 de salvamento de registradores e flags no momento em que a thread retoma, e retorno por salto indireto; implementado no backend existente, com alinhamento de pilha e preservação adicional de XMM0–XMM5 |
| Xenos `ProfileMgr::ConfigData` | Modelo de configuração adaptado aos métodos e opções do Injector; licença e origem no header |
| Xenos `ValidateContext`, `LoadImageFile`, `DlgModules::RefreshList` | Referência de organização; implementação própria de validação e inspeção com APIs documentadas |
| Injector | Parser PE com limites, formato de perfil, atualização assíncrona, controle da operação, cancelamento, gerenciamento de timeout, proteção das páginas e registro de unwind por `RtlAddFunctionTable` |

Não foi integrada a biblioteca completa do Blackbone. Não há driver, WDK, DIA, download de símbolos, AsmJit ou wow64ext no build. As licenças MIT completas e a identificação das adaptações estão em `THIRD_PARTY_NOTICES.md` e `licenses/`.

## Mudanças de comportamento

- A lista de processos não bloqueia a renderização e não publica referências modificáveis por outra thread.
- A seleção de DLL verifica o PE; o motor repete a validação antes de executar, inclusive se o arquivo mudou após a seleção.
- Arquivos PE inválidos, executáveis, CLR, arquitetura desconhecida ou incompatível recebem erros explícitos.
- Manual Map verifica cabeçalhos, seções, diretórios, imports e relocations. Tenta a base preferida e aplica relocations quando necessário. Revalida os RVAs usados após rebasing.
- Tabelas de unwind x64 são registradas antes do `DllMain`; uma falha de inicialização limpa a tabela quando possível. Resultado indeterminado mantém o armazenamento que ainda pode estar referenciado.
- Código remoto é escrito em páginas RW e depois muda para RX. As seções do módulo recebem as proteções declaradas no PE; os cabeçalhos ficam somente para leitura.
- Endereços de funções do sistema são calculados na base do módulo correspondente no alvo; não se presume que o endereço absoluto local seja igual ao remoto.
- Thread Hijack não restaura um contexto antigo sobre uma thread já em execução e não libera o código de retorno prematuramente. Suas páginas ficam retidas até o processo terminar, mesmo após sucesso.
- O worker é encerrado com `request_stop` e `join`. Delay pode ser cancelado. Após início remoto, cancelamento encerra a espera local e publica resultado indeterminado.
- O timeout se aplica à execução do backend e às suas esperas; não representa um limite rígido para leitura de arquivo ou APIs síncronas do Windows.
- A política `ProhibitDynamicCode` do alvo é consultada e respeitada antes de Manual Map ou Thread Hijack. Não se tenta modificá-la.
- O privilégio de debug deixou de ser ativado automaticamente. Falha de acesso é diagnosticada, sem promover handles ou alterar proteções do sistema.
- `Modules` e `Cancel` foram acrescentados à interface. A seção de perfis e o campo de timeout foram removidos a pedido do usuário; a espera interna usa o limite padrão do motor. A tela distingue `Indeterminate` de falha e mostra `Unknown` para o estado da DLL nesse caso.

## Arquivos

Criados:

- `src/core/win32.hpp`.
- `src/core/diagnostics.hpp` e `.cpp`.
- `src/core/loaders.hpp` e `.cpp`.
- `src/core/modules.hpp` e `.cpp`.
- `src/core/pe_image.hpp` e `.cpp`.
- `src/core/profile.hpp` e `.cpp`.
- `src/ui/file_dialog.hpp`, `.cpp` e `src/ui/settings.cpp`.
- `vendor/blackbone/HandleGuard.hpp`.
- `licenses/Xenos.txt`, `licenses/Blackbone.txt`, `THIRD_PARTY_NOTICES.md`.
- `tests/core_tests.cpp`, `tests/fixtures/target.cpp`, `tests/fixtures/dll.cpp` e seus dois `CMakeLists.txt`.
- `docs/refactoring.md`.

Modificados: `src/inject.*`, `src/process.*`, `src/main.cpp`, `src/ui/ui.hpp`, `src/ui/pages.cpp`, `CMakeLists.txt`, `Injector.vcxproj`, `Injector.vcxproj.filters`, `README.md` e `preview.png`.

Nenhum arquivo original foi removido. Foram retiradas implementações duplicadas de leitura PE/arquivo/UTF-8, fechamento manual de handles nos módulos refatorados, abertura redundante do processo para consultas, worker destacado e ativação automática de privilégio de debug. A DLL original `tests/injtest.dll` foi preservada.

## Testes e ambiente

Ambiente observado: Windows 10 Pro x64 `10.0.19045`, MSVC `19.44.35229`, toolset v143, Windows SDK `10.0.26100.0`, Visual Studio Community 2022.

A suíte cria `InjectorTestTarget.exe`, eventos próprios e DLLs de teste compiladas a partir dos fontes incluídos. Não usa navegador, bloco de notas, processo protegido ou aplicação de terceiros como alvo. O helper x86 também foi compilado a partir de `tests/fixtures/target.cpp`.

Verificações funcionais:

- LoadLibrary, Manual Map e Thread Hijack: `DllMain` executou no filho; evento confirmou a execução; a thread principal continuou e encerrou com código zero.
- O alvo reserva a base preferida da DLL para exercitar relocations no Manual Map. A DLL compara um ponteiro relocado e usa `RtlLookupFunctionEntry` para verificar o registro real do unwind, em todos os métodos.
- DLL lenta: timeout de 100 ms em LoadLibrary e Manual Map; resultado indeterminado, nenhum retry; a DLL terminou depois e o alvo permaneceu vivo. Isso verifica a retenção do armazenamento ainda em uso.
- DLL que retorna `FALSE`: falha de inicialização, sem anunciar sucesso.
- Processo criado com política de proibição de código dinâmico: operação bloqueada antes da execução.
- Processo x86 real: arquitetura reconhecida; DLL x64 rejeitada. PE32 real também foi inspecionado.
- Processo e módulos com identidade divergente: rejeição de PID obsoleto.
- Consulta repetida de módulos: contagem de handles sem crescimento; atualização de processos concorrente com leitores.
- PE vazio, truncado, offset negativo, seção fora do arquivo e diretório fora da imagem: rejeitados.
- Caminho de DLL e perfil com acentos: leitura, gravação e carregamento corretos.
- Perfil inválido: estado anterior preservado; carregar duas vezes não acumula dados.
- Método inválido, arquivo inexistente, operação concorrente, cancelamento antes de executar e encerramento com worker ativo: tratados.

Os logs completos de execução ficam em `build/refactor-cmake/`, fora do Git. Os resultados finais de compilação e testes estão na tabela a seguir.

| Verificação | Resultado |
| --- | --- |
| MSBuild Release x64 | Executável compilado em `build/refactor/Injector.exe` |
| MSBuild Debug x64 | Executável compilado em `build/refactor-debug/Injector.exe` |
| CMake Release x64 | Compilado; suíte com helper WOW64: 65 verificações aprovadas |
| CMake Debug x64 | Compilado; CTest aprovado, 62 verificações sem o helper WOW64 opcional |
| Interface | Capturas reais de Injector, Settings, About e painel Modules; rolagem e quebra de caminhos na lista verificadas visualmente |
| Windows 11 | Não executado |
| Carregamento x86 / ARM64 | Não implementado nem anunciado |

Reprodução dos testes principais:

```powershell
cmake -S . -B build/tests -G "Visual Studio 17 2022" -A x64 -DINJECTOR_BUILD_TESTS=ON
cmake --build build/tests --config Release
ctest --test-dir build/tests -C Release --output-on-failure
```

Teste adicional com alvo x86:

```powershell
cmake -S tests/fixtures -B build/fixtures-x86 -G "Visual Studio 17 2022" -A Win32
cmake --build build/fixtures-x86 --config Release
build/tests/Release/InjectorCoreTests.exe build/tests/tests/fixtures/Release/InjectorTestTarget.exe build/tests/tests/fixtures/dlls/Release/InjectorFixtureGood.dll build/tests/tests/fixtures/dlls/Release/InjectorFixtureSlow.dll build/tests/tests/fixtures/dlls/Release/InjectorFixtureReject.dll build/fixtures-x86/Release/InjectorTestTarget.exe
```

## Limitações restantes

1. Windows 11, ARM64, CET/shadow stack e estados estendidos AVX/XSTATE não foram validados. Thread Hijack preserva o estado x64 usado pelos testes e XMM0–XMM5; isso não é garantia para qualquer aplicação ou workload vetorial.
2. Manual Map não implementa TLS, delay imports, load configuration/security cookies, CLR, SxS/activation contexts ou resolução completa de API sets. TLS, delay imports e load configuration são rejeitados antes da execução; outros casos podem falhar com diagnóstico. Use LoadLibrary para DLLs que dependem desses serviços.
3. A tabela Modules usa Toolhelp e mostra módulos do loader. Não detecta imagens manuais pela varredura de memória e não oferece unload.
4. DLLs inicializadas com sucesso permanecem carregadas. Dependências abertas durante um Manual Map que falhou podem permanecer carregadas no alvo. Não há transação completa de descarregamento/rollback de dependências.
5. Resultados indeterminados podem deixar memória no alvo até seu encerramento. Thread Hijack também mantém seu pequeno bootstrap após sucesso, pois o resultado publicado precede o final do retorno. Nenhuma thread é terminada à força para recuperar memória.
6. O teste de handles cobre a enumeração de módulos e o ciclo normal dos processos de teste; não foi uma auditoria formal de todos os recursos gráficos ou de todas as APIs do Windows.
7. A suíte não comprova compatibilidade com DLLs arbitrárias, processos protegidos, sandboxes, ambientes com todas as combinações de CFG/CIG/CET ou produtos de segurança. Não foram implementados contornos dessas proteções.
8. As configurações são persistidas automaticamente; não há controles de perfil na interface nem seleção persistente de PID.


## Ajuste visual do painel de módulos

O painel usa os mesmos recursos visuais das páginas existentes: fontes Manrope, cores do tema, ícones, cantos arredondados e barra de rolagem compartilhada com Process List e Injection Log. A grade padrão foi substituída por uma lista com nome, base, tamanho e caminho completo com quebra de linha. Os botões reutilizáveis `action_button` e `close_button` foram adicionados a `widgets`; a consulta dos módulos não foi alterada. Refresh, fechamento por botão/X/Escape e bloqueio de cliques no fundo foram conferidos em capturas da aplicação.
