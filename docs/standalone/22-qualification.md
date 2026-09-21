# Fase 22 — Matriz de qualificação por API/bitness

## Objetivo

Separar Target de Supported com evidência por rota.

## Dependências

Rotas candidatas implementadas.

## Fora de escopo

- Claim por intenção
- Compile/CI como única prova

## Checklist de implementação

- [ ] Matriz API x bitness x Native/Bridge/Synthetic.
- [ ] Color/Depth/Motion/HDR/NR/MFG/GPU-only por rota.
- [ ] Acquisition real.
- [ ] Sync correta.
- [ ] Ownership/lifetime.
- [ ] NR executa.
- [ ] Compose correto.
- [ ] Resize/recreate.
- [ ] Failure path.
- [ ] Sem CPU pixel transport.
- [ ] Steady resource discipline.
- [ ] MFG separado de NR.

## Revisão obrigatória

- [ ] Target != Supported.
- [ ] Blocked com razão técnica é válido.
- [ ] NR universal != MFG universal.
- [ ] x86/x64 gates separados.

## Validação rápida

- [ ] Frontend correspondente do harness.
- [ ] Cenários padrão por API.
- [ ] Depois jogos reais por API/engine.

## Gate de conclusão

- [ ] Cada célula tem estado/evidência.
- [ ] Claims inequívocas.
- [ ] Quick repro via harness.

## Entregáveis

- Qualification matrix
- Evidence per route

## Próxima fase

Fase 23.
