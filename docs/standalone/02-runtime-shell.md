# Fase 02 — Standalone runtime shell

## Objetivo

Criar lifecycle, bootstrap e configuração do host sem ainda executar DLSS 5.

## Dependências

Fases 00–01.

## Fora de escopo

- Executor real
- Carrier gráfico completo
- MFG completo

## Implementação

- [ ] Definir init/shutdown idempotentes.
- [ ] Separar bootstrap/proxy do carrier gráfico; uma DLL de entrada não deve conter policy da API.
- [ ] RuntimeConfig compacto por snapshot/generation.
- [ ] Registrar providers/executors por capability.
- [ ] Logging/status mínimo com failure reason estável.
- [ ] Eliminar OptiScaler Config/State e patcher Python como requisito de runtime.
- [ ] Disabled path quase pass-through.
- [ ] Definir thread ownership e state transitions.

## Revisão obrigatória

- [ ] COM/module/hook ownership explícito.
- [ ] Partial init/shutdown limpa apenas o que possui.
- [ ] Bootstrap não pressupõe que D3D12 seja a API do jogo.
- [ ] Sem filesystem/config parsing no frame path.
- [ ] Global mutable state precisa de owner e motivo.

## Validação rápida

- [ ] Abrir/fechar shell repetidamente com fake components.
- [ ] Injetar falha em cada estágio de init.
- [ ] Medir disabled path no harness.

## Gate

- [ ] Shutdown sem leak/thread pendente.
- [ ] Disabled não altera output.
- [ ] Bootstrap está desacoplado de Acquire/Execute específicos.

## Próxima fase

Fase 03.
