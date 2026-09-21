# Fase 02 — Evidência do runtime shell

## Runtime shell

Novos componentes:

- `RuntimeConfig`: generation, Enabled, Auto/BestQuality/Performance/Custom e target FPS;
- `RuntimeComponentRegistry`: array fixo de 16 entries, sem heap;
- `RuntimeShell`: lifecycle, status e reconfigure;
- `RuntimeBootstrap`: startup transacional + rollback.

A configuração default é disabled. Enable precisa ser explícito.

O shell não contém filesystem, COM, hooks, strings de config nem tipos de API específicos.

### Ownership

`RuntimeShell` é dono de:

- snapshot de config;
- status;
- registry.

O registry mutável é privado e só `RuntimeBootstrap` pode preenchê-lo. Leitores recebem visão const.

### Config generation

- geração menor: rejeitada;
- mesma geração + mesmo conteúdo: idempotente;
- mesma geração + conteúdo diferente: rejeitada;
- geração maior válida: aplicada.

O standalone não reutiliza `UserMode::Balanced`; o contrato novo usa somente
Auto / BestQuality / Performance / Custom.

### Bootstrap

`Start()`:

1. é no-op para o mesmo plano já ativo;
2. rejeita plano diferente quando já iniciado;
3. valida config;
4. registra capabilities;
5. faz rollback completo se qualquer componente for inválido.

`Stop()` é idempotente.

## GameProbe

O antigo `GameProbe.cpp` de ~628 linhas foi dividido em:

| Arquivo | Linhas |
|---|---:|
| `GameProbe.cpp` | 135 |
| `GameProbeInspect.cpp` | 157 |
| `GameProbeDetection.cpp` | 201 |
| `GameProbeSupport.cpp` | 107 |
| `GameProbeInternal.hpp` | 48 |

Responsabilidades:

- Inspect: leitura/PE imports;
- Detection: heurísticas API/capabilities/DXVK;
- Probe: orchestration;
- Support: integrated capabilities/install-support.

Comparação automática de corpos mostrou equivalência em todas as funções movidas. `ReadPrefix`
teve apenas a remoção do parâmetro default interno, que nunca possuía caller com segundo argumento.

Na terceira revisão, somente comentários narrativos foram reduzidos; comparação ignorando comentários
confirmou código equivalente nos arquivos tocados.

## CMake

Antes: um `CMakeLists.txt` de 266 linhas.

Depois:

| Arquivo | Linhas |
|---|---:|
| `CMakeLists.txt` | 13 |
| `NRFusionCore.cmake` | 86 |
| `NRFusionTests.cmake` | 18 |
| `NRFusionTools.cmake` | 6 |
| `NRFusionWindows.cmake` | 121 |

Comparação de target/source sets:

- targets antigos removidos: 0;
- tests antigos removidos: 0;
- fontes antigas removidas: 0;
- novo test target: `nrfusion_runtime_shell_tests`;
- novas fontes: split do GameProbe + RuntimeShell/RuntimeBootstrap.

A terceira revisão reconfirmou os mesmos sets após a limpeza de comentários.

Configure local do CMake modular: **PASS** na implementação original.

## Performance / validação

Runtime shell local:

```text
C++20 -Wall -Wextra -Wpedantic -Werror
runtime shell compile/test: PASS
bootstrap rollback/idempotence: PASS
invalid config/enum/component: PASS
```

Disabled path, 5.000.000 reconfigures idempotentes, build `-O3`:

```text
revisão 2: ~3.18 ns/call, 0 allocations
revisão 3: ~3.175 ns/call, 0 allocations
```

Esse número mede somente o shell portátil no ambiente local; não representa frametime de jogo.

## Revisão independente — segunda passada

Foram corrigidos dois casos fail-closed que os testes originais não cobriam:

- plano ativo com Provider+Executor aceitava retry contendo só Provider como o mesmo plano;
- `RuntimeComponentRegistry::Supports()` aceitava capability mask zero quando a chave existia.

Correções:

- `MatchesPlan()` canonicaliza o plano e exige mesmo conjunto/máscaras;
- `Supports()` rejeita capability mask zero;
- regressões adicionadas a `runtime_shell_tests`.

Commits: `33ca0214a2636a7db7f5279eb73df23a0b6a9fb0`,
`0adfacbdb13642a616ae0722bb378d758ec6ff42`.

## Revisão independente — terceira passada

Foram encontrados três pontos adicionais:

- `RuntimeConfig{}` tinha `enabled=true`, permitindo bootstrap default em estado Running sem enable explícito;
- a evidência de rollback por config inválida não era exercitada através de `RuntimeBootstrap::Start()`;
- `RuntimeBootstrap.hpp` usava `std::uint8_t` via include transitivo.

Correções:

- default alterado para `enabled=false`;
- regressão adicionada para config inválida via bootstrap e para capability mask zero no registro;
- `<cstdint>` incluído diretamente;
- comentários narrativos movidos durante o split foram reduzidos sem alteração de código.

## Source-size

Maior arquivo first-party novo/tocado nesta fase: `GameProbeDetection.cpp`, 201 linhas.
Nenhum arquivo tocado >300 e nenhum bloco de comentário restante >120 caracteres.

## Limites deliberados

- ainda não existe executor standalone;
- ainda não existe carrier/proxy standalone;
- o shell não é consumido pelo frame path;
- concorrência render/UI fica para o snapshot operacional do `NrSession` na Fase 06;
- build_dist/installer continuam legacy e só mudam nas fases próprias;
- nenhum full CI foi disparado nesta fase.

## Gate Fase 02

Fechado após três passadas. Fase 03 não foi iniciada por esta revisão.
