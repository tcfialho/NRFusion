# Fase 23 — A/B final e cutover do OptiScaler

## Objetivo

Trocar o host e a distribuição principal somente após equivalência, performance, packaging e rollback claros.

## Dependências

Fase 22.

## Fora de escopo

- Apagar fallback cedo
- Claim de GPU sem hardware real

## Implementação

- [ ] Comparar CPU p50/p95/p99, allocations, locks, resource creates, queries e copies.
- [ ] Comparar resource ledger e peak VRAM real.
- [ ] Executar A/B real: GPU frame ms, NR ms, FPS, 1% low, p95/p99 e MFG pacing.
- [ ] Validar menu/config/recovery e rotas anunciadas.
- [ ] Substituir build_dist/installer que hoje empacotam OptiScaler por bootstrap/carriers/Host64 standalone.
- [ ] Empacotar x64/x86 sem carregar módulos de APIs desnecessárias quando separáveis.
- [ ] Manter OptiScaler como referência/fallback durante preview.
- [ ] Definir critérios objetivos de rollback.
- [ ] Remover dependência primária só após release de transição estável.

## Revisão obrigatória

- [ ] A/B usa feature/configuração equivalente.
- [ ] Warm-up/carga comparáveis.
- [ ] Resultado negativo é blocker ou limitação explícita.
- [ ] Installer/uninstaller não deixa proxy conflitante do OptiScaler.
- [ ] Cutover não depende de um único jogo/API.

## Validação rápida

- [ ] Harness suite completa antes de RC.
- [ ] Instalação limpa, upgrade e uninstall em layout de teste.
- [ ] Hardware real nas rotas prioritárias.
- [ ] Stress resize/reset/reconfigure/MFG.

## Gate

- [ ] Rotas anunciadas estão Hardware-qualified.
- [ ] 0 steady-state allocation/resource creation no normal path.
- [ ] VRAM equivalente sem regressão.
- [ ] Host CPU abaixo do baseline equivalente.
- [ ] Packaging não depende de OptiScaler.
- [ ] Rollback/fallback testado.

## Próxima fase

Fim.
