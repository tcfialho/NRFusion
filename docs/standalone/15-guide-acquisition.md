# Fase 15 — Universal guide acquisition

## Objetivo

Fazer game sem DLSS ser caminho normal com melhor guide disponível.

## Dependências

Carriers relevantes.

## Fora de escopo

- Inventar provenance
- Exigir guides nativos

## Checklist de implementação

- [ ] Motion priority: Native -> DLSS contract -> NVOF -> shader -> Zero.
- [ ] Depth reliability central.
- [ ] Exposure/HDR source.
- [ ] Provenance no FrameContract.
- [ ] Reset histories em mudança material.
- [ ] Policy usa capabilities reais.
- [ ] Tabela API/provider de sources.

## Revisão obrigatória

- [ ] Non-null != reliable.
- [ ] Zero nunca Native.
- [ ] NVOF/shader têm custo/limites explícitos.
- [ ] Camera cut invalida history.

## Validação rápida

- [ ] Fake executor alterna provenance.
- [ ] Carriers sem guides.
- [ ] Status reporta fonte real.

## Gate de conclusão

- [ ] Todas rotas têm estratégia.
- [ ] Sem DLSS não implica unsupported.
- [ ] Fallback observável.

## Entregáveis

- Guide policy
- Matriz de fontes

## Próxima fase

Fase 16.
