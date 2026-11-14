# Resultados experimentais

Cada campanha deve possuir um identificador único e um arquivo próprio. Não editar resultados brutos depois da execução. Correções de interpretação devem ser registradas em um relatório complementar com data e referência ao artefato original.

## Registro existente

### G1 benchmark sintético — smoke test

- Referência: `docs/35-etapa-10-benchmark-validacao.md`.
- Commit de código reportado: `890380b44ba66b47cb2870f13c75dc990706e921`.
- CI benchmark reportado: run `43529785578`.
- Dados: fixture sintético CC0-1.0, seis documentos, 559 bytes.
- Execução: uma época, semente 42, seis configurações.
- BPB teste G1: 7,9658–8,8871.
- BPB teste baseline softmax-256 unigram: 4,9958.
- Interpretação: baseline foi melhor nesse fixture.
- Limite: não representa corpus linguístico real, não valida generalização e não mede hardware de referência de 4 GB.

Os valores acima são reproduzidos da documentação do benchmark já existente. O JSONL completo permanece como artifact do workflow indicado na documentação; este índice não substitui o artefato original.

## Estado de evidência

Não há ainda resultado de campanha com corpus real/licenciado nem medição física em hardware de 4 GB registrada neste repositório. Não preencher lacunas com valores estimados ou dados sintéticos apresentados como reais.

## Convenção de nomes

Use `<experiment-id>.manifest.json` para metadados e `<experiment-id>.jsonl` para saída bruta, mantendo arquivos grandes fora do Git quando necessário. Publique-os como artifacts versionados e registre URL/identificador e SHA-256 no manifesto. Só versione relatórios pequenos, não sensíveis e reproduzíveis.
