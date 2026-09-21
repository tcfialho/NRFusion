# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle/bootstrap/configuração sem concentrar tudo em um `Runtime.cpp` gigante.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- Carrier completo
- MFG completo

## Implementação

- [ ] Separar bootstrap/proxy, runtime lifecycle, config snapshot e registry em responsabilidades distintas.
- [ ] Definir init/shutdown idempotentes.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registrar providers/executors por capability.
- [ ] Logging/status mínimo com failure reason estável.
- [ ] Eliminar OptiScaler Config/State e patcher Python em runtime.
- [ ] Disabled path quase pass-through.
- [ ] Definir thread ownership/state transitions.
- [ ] Manter cada módulo <=300 linhas; preferir alvo <=250.

## Revisão obrigatória

- [ ] COM/module/hook ownership explícito.
- [ ] Partial init/shutdown limpa apenas o que possui.
- [ ] Bootstrap não pressupõe D3D12 como API do jogo.
- [ ] Sem filesystem/config parsing no frame path.
- [ ] Nenhuma classe agrega bootstrap + policy + carrier.

## Validação rápida

- [ ] Abrir/fechar shell com fake components.
- [ ] Injetar falha por estágio.
- [ ] Medir disabled path.
- [ ] Rodar checker de 300 linhas sobre arquivos novos/tocados.

## Gate

- [ ] Shutdown sem leak/thread.
- [ ] Disabled não altera output.
- [ ] Bootstrap desacoplado de Acquire/Execute.
- [ ] Nenhum arquivo novo/tocado >300 linhas.

## Próxima fase

Fase 03.
