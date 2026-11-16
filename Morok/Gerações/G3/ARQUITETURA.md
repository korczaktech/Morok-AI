# G3 — contrato arquitetural e integração

## 1. Regra estrutural

A G3 utiliza o motor Morok existente. Não será criado um segundo Core, um segundo runtime de inferência ou uma cópia do motor dentro de `Gerações/G3/shared/`.

A infraestrutura compartilhada permanece na biblioteca pública `Morok::Core`. O contrato neutro de execução de modelos está em:

- `include/morok/model/model_backend.hpp`

Esse contrato descreve como o runtime conversa com um backend neural, sem conhecer sua arquitetura interna, pesos, dimensões ou implementação matemática.

A G3 implementará seu backend arquitetural dentro do próprio diretório da geração e o conectará ao contrato público. A implementação de GRU, RMSNorm, projeção, weight tying e kernels específicos da G3 pertence ao backend da G3; isso não autoriza duplicar o runtime ou o Core.

## 2. O que já existe e o que foi integrado

| Componente | Local | Responsabilidade |
|---|---|---|
| Core compartilhado | `Morok::Core` | Tipos, erros, UTF-8, memória e subsistemas neutros |
| Contrato de backend | `morok/model/model_backend.hpp` | Interface comum para execução incremental |
| Motor neural G1 | `Morok::G1` | Arquitetura recorrente de duas escalas já existente |
| Adaptador G1 | `morok/model/g1_backend.hpp` | Expõe o motor G1 pelo contrato comum |
| Motor Tsukuyomi G2 | `Morok::G2::Tsukuyomi` | Implementação G2 existente, preservada e isolada |
| Backend G3 | `Gerações/G3/` | Será implementado nas próximas etapas usando o contrato |

O adaptador G1 não copia nem substitui o motor. Ele encaminha as operações ao forward pass existente. O objeto de parâmetros continua pertencendo ao chamador e deve permanecer vivo e imutável enquanto o adaptador estiver em uso.

A G2 permanece sem dependência da implementação privada da G1. Nenhum código da G2 precisa ser migrado para concluir esta etapa.

## 3. Contrato público

`morok::model::ModelBackend` define:

- `descriptor()`: identidade da geração e arquitetura, vocabulário, contagem de parâmetros e tamanho/alinhamento do estado.
- `reset_state(state)`: inicializa o estado de uma sessão independente.
- `consume_byte(state, byte)`: incorpora um byte observado sem compartilhar estado entre sessões.
- `next_token_log_probabilities(state, output)`: produz uma log-probabilidade por elemento do vocabulário.

A fachada `morok::model::ModelSession`, também publicada pelo Core, conecta um
backend a um buffer de estado fornecido pelo chamador. Ela valida descritor,
tamanho e alinhamento, exige reset antes de entrada/inferência e impede cópia
acidental do objeto de sessão. Não possui pesos, kernels, gerador ou otimizador:
é apenas o caminho comum de ciclo de vida e despacho para o backend existente.

O estado é opaco para o Core. O chamador aloca e possui o buffer; o backend só interpreta o estado criado por sua própria implementação. O tamanho exato é declarado no descritor. O alinhamento é informado para que futuros backends possam usar armazenamento especializado.

As saídas são log-probabilidades, não logits arbitrários. Entradas não suportadas pelo vocabulário devem ser representadas por menos infinito. A quantidade de saídas precisa corresponder exatamente ao tamanho do vocabulário declarado.

As operações retornam `morok::common::Status`. Erros de tamanho, estado inválido e falhas numéricas não devem resultar em atualização parcial do estado ou da saída.

## 4. Identidade de arquitetura

O contrato não presume que todas as gerações tenham a mesma arquitetura.

- G1: geração `1`, arquitetura `0x0101`, vocabulário de 256 bytes, 1.000 parâmetros e estado recorrente próprio.
- G3: geração `3`, arquitetura inicial reservada `0x0301`, vocabulário de 261 IDs, 198.149 parâmetros conforme a especificação revisada e estado de referência de 1.024 bytes em FP32.

O identificador G3 é uma reserva para a arquitetura GRU-RMSNorm-Weight-Tying. Deve permanecer estável depois que o formato de checkpoint for publicado. A contagem real de parâmetros deverá ser calculada pela implementação e confrontada com a especificação antes de aprovar a etapa neural.

O contrato aceita vocabulários e estados de tamanhos diferentes. Portanto, a G1 não precisa ser alterada para imitar a GRU da G3, e a G3 não precisa copiar o código da G1.

## 5. Limites de responsabilidade

### Core Morok
- Define tipos e contratos neutros.
- Não contém parâmetros privados de uma geração.
- Não contém kernels exclusivos da G1, G2 ou G3.
- Não escolhe hiperparâmetros de treinamento de uma geração.

### Backend de cada geração
- Implementa seu forward pass e estado recorrente.
- Informa dimensões, arquitetura e vocabulário.
- Converte sua distribuição de saída para log-probabilidades.
- Valida seu próprio estado e suas restrições numéricas.

### Treinamento
- Será conectado em etapa posterior.
- Não faz parte da interface mínima de inferência desta etapa.
- SFT, BPTT, otimizador, RL e persistência terão contratos próprios, sem acoplar o Core a um algoritmo específico.

### Perfis G3
Mokosh, Tsukuyomi, Artemis e Apollo serão políticas/perfis construídos sobre a infraestrutura e o backend da G3. Não terão cópias independentes do Core ou do runtime.

## 6. Compatibilidade e isolamento

- A G1 mantém seus executáveis, parâmetros, treinamento, checkpoint e resultados atuais.
- O adaptador G1 é aditivo e usa as funções existentes de avanço e avaliação.
- A G2 continua compilando como `Morok::G2::Tsukuyomi`, sem importar fontes privadas da G1.
- O contrato comum é instalado junto aos cabeçalhos públicos de Morok.
- A G3 depende de `Morok::Core` e do contrato público, nunca de caminhos privados da G1/G2.
- Mudanças futuras no contrato exigem testes de compatibilidade e versionamento explícito.

## 7. Critérios de conclusão da Etapa 1

- [x] Contrato neutro de backend disponível em cabeçalho público.
- [x] Adaptador da G1 utiliza o motor existente, sem duplicá-lo.
- [x] Estado de sessão é fornecido pelo chamador e não é compartilhado.
- [x] Saída padronizada como log-probabilidades por item de vocabulário.
- [x] Testes cobrem descritor, reset, transições incrementais, normalização e buffers inválidos.
- [x] CMake integra o adaptador à biblioteca G1 existente.
- [x] G2 permanece isolada da implementação privada da G1.
- [x] Documentação deixa explícito que a G3 não criará um motor/Core paralelo.
- [x] CI da G3 valida contrato, adaptador G1, G2 e isolamento arquitetural.

A Etapa 1 não implementa a GRU G3, treinamento SFT/RL ou inferência final. Essas entregas pertencem às etapas posteriores.


## 8. Representação linguística G3 — Etapa 2

A representação byte-level foi integrada à superfície compartilhada do Core,
sem criar um subsistema de tokenização paralelo dentro da G3:

- Cabeçalho público: `include/morok/lang/byte_tokenizer.hpp`.
- Implementação: `src/lang/byte_tokenizer.cpp`.
- API: `morok::lang::ByteTokenizer`.
- Vocabulário: 261 IDs estáveis — bytes 0–255, PAD 256, BOS 257, EOS 258,
  USR 259 e BOT 260.
- Mensagens: `[BOS?][USR|BOT][bytes UTF-8][EOS?]`; o método de append permite
  compor vários turnos sem inserir BOS/EOS adicionais.
- Entrada inválida: substituição por U+FFFD por padrão, com opção estrita de
  rejeição. A ocorrência de substituição é devolvida para diagnóstico.
- Normalização: Unicode NFC por padrão, configurável para desativação. Não há
  remoção automática de acentos, pontuação, caixa ou espaços.
- Saída: IDs de byte são reconstruídos e validados como UTF-8 antes da entrega;
  EOS encerra a decodificação e outros tokens de controle são rejeitados. O limite
  máximo de bytes recua até uma fronteira UTF-8 válida e informa se houve corte.
  Substituições de bytes inválidos na saída também são sinalizadas no resultado.
- NFC é implementada pela biblioteca ICU (componente uc), não por uma tabela
  Unicode parcial ou uma normalização artesanal.

O Core continua responsável apenas por contratos e infraestrutura genéricos.
O tokenizer é um utilitário compartilhado de representação textual, não um
novo motor de inferência. Treinamento, dataset, SFT, RL e a arquitetura neural
G3 permanecem fora do escopo desta etapa.

### Critérios de conclusão da Etapa 2

- [x] IDs byte-level e cinco tokens especiais distintos e estáveis.
- [x] Conversão de UTF-8 em IDs de byte, preservando os bytes do texto normalizado.
- [x] Marcadores BOS, EOS, USR e BOT e composição de múltiplas mensagens.
- [x] Validação estrita ou substituição configurável de UTF-8 inválido.
- [x] NFC configurável, habilitada por padrão, via ICU.
- [x] Validação de UTF-8 na decodificação antes de entregar texto.
- [x] Limite de saída que recua até uma fronteira UTF-8 e sinaliza truncamento.
- [x] Testes de vocabulário, português acentuado, NFC, dados inválidos,
      políticas de rejeição, tokens de controle e round-trip.
- [x] CMake e workflow G3 integrados ao Core existente.


## 9. Aprendizado por reforço — Etapa 3

A implementação de RL pertence a `Gerações/G3/rl/` e reutiliza
`Morok::Core`. Não cria Core, runtime de inferência, estado de modelo ou
armazenamento de pesos paralelo.

### 9.1 Contrato de política

- Cabeçalho: `rl/include/morok/g3/rl/reinforce.hpp`.
- Implementação: `rl/src/reinforce.cpp`.
- Testes: `rl/tests/test_reinforce.cpp`.
- A política fornece uma distribuição normalizada de 261 IDs para cada
  contexto/prefixo.
- O algoritmo bloqueia PAD, BOS, USR e BOT como tokens de resposta; bytes e
  EOS permanecem disponíveis.
- A referência SFT é recebida como `const InferencePolicy&`, uma interface sem
  método de atualização: pode executar inferência, mas não recebe gradientes.
- A política treinável recebe gradientes de logits acumulados para o lote e
  aplica uma única atualização de otimizador com gradientes médios sobre as candidatas do lote. Os pesos e o otimizador continuam
  pertencendo ao backend neural da geração.

### 9.2 REINFORCE e amostragem

- Até quatro candidatas por pergunta, configurável.
- Amostragem com temperatura e Top-K configuráveis; defaults experimentais
  temperatura 0,7 e Top-K 10.
- Seed determinística configurável.
- REINFORCE usa `A = recompensa - baseline_categoria`.
- Baseline exponencial móvel por categoria, com coeficiente configurável
  (default 0,10).
- Gamma default 1,0; ponderação temporal aplicada por token.
- Gradientes são acumulados para todas as candidatas antes da chamada única
  `apply_batch_gradients`, evitando que candidatas do mesmo lote sejam
  geradas com pesos parcialmente atualizados.

### 9.3 Recompensas auditáveis

A rubrica expõe quatro componentes separados: correção, aderência à intenção,
coerência e formato. Pesos default: 0,70 / 0,15 / 0,05 / 0,10; a soma deve ser
1 dentro da tolerância numérica. Cada componente deve estar em [-1, +1]. A
recompensa composta é limitada a [-1, +1].

Penalidades configuráveis:
- resposta vazia;
- repetição de bytes consecutivos e de sequências de bytes repetidas;
- cópia de sequência contígua de bytes do prompt;
- excesso de comprimento somente quando um limite justificado for configurado.

Não há penalidade automática de comprimento por padrão. A semântica não é
inferida por distância de edição ou n-gramas. O avaliador deve ter identidade,
versão de rubrica e origem dos rótulos distintas da política treinada. A função
`audit_evaluator` compara resultados com intervalos aprovados em casos
rotulados; o treinamento exige esses casos e bloqueia atualizações se a auditoria falhar. Essa auditoria não substitui revisão humana periódica.

### 9.4 KL de referência

A divergência KL é calculada entre a distribuição atual e a distribuição da
referência SFT para os mesmos contextos e prefixos. Beta default 0,02,
configurável. O gradiente da regularização é incorporado aos gradientes de
logits do REINFORCE. Probabilidades de referência iguais a zero são limitadas
por epsilon configurável para evitar NaN/Inf; a divergência observada continua
registrada no relatório.

### 9.5 Proteção contra reward hacking

O módulo mede e penaliza resposta vazia, repetição e cópia da pergunta; o
comprimento só é penalizado com limite explicitamente configurado. Respostas
genéricas dependem da rubrica de correção/aderência, não de uma regra lexical.
Os testes cobrem resposta vazia, genérica, repetida, exploração de avaliador,
KL positiva e comparação SFT/RL no mesmo conjunto fechado. Casos de encerramento
prematuro e respostas longas sem conteúdo devem ser incluídos nos conjuntos de
auditoria do avaliador.

### 9.6 Comparação e métricas

`compare_sft_and_rl` avalia snapshots distintos no mesmo conjunto fechado,
usando decodificação gulosa e o mesmo avaliador. Relata recompensa média,
comprimento médio, taxa de respostas vazias e taxa de repetição, além da
diferença de recompensa RL menos SFT. O relatório retém configuração, identidade
 e versão da rubrica, origem dos rótulos, componentes da recompensa, tokens
 amostrados e log-probabilidades sob a distribuição de amostragem. O estado
  recuperável do treinador guarda configuração, baseline por categoria, estado
  do gerador aleatório e identidades da política, referência e avaliador; pesos
  e estado do otimizador continuam no checkpoint do backend.

### Critérios de conclusão da Etapa 3

- [x] Contrato de política treinável e referência somente de inferência.
- [x] REINFORCE com baseline EMA por categoria.
- [x] Amostragem configurável com até quatro candidatas, Top-K, temperatura e seed.
- [x] Composição de recompensa por critérios independentes e penalidades explícitas.
- [x] Auditoria de avaliador com identidade, versão, origem e casos rotulados.
- [x] Penalidade KL em relação à referência SFT e gradiente correspondente.
- [x] Proteções e testes iniciais contra reward hacking.
- [x] Comparação SFT/RL no mesmo conjunto fechado.
- [x] Captura e restauração de baseline, RNG, configuração e proveniência do treino.
- [x] Biblioteca, testes CTest e workflow G3 integrados.
- [ ] Integração com pesos reais e BPTT da GRU G3: depende da implementação
      do backend neural treinável, ainda não entregue nas etapas anteriores.


## 10. Inferência e exportação — Fase 4

A Fase 4 adiciona utilitários G3 específicos em `Gerações/G3/inference/`,
reutilizando `Morok::Core` e o contrato público `ModelBackend`. Não cria
outro runtime nem altera os fontes das gerações anteriores.

### 10.1 Decodificação

- `generator.hpp/.cpp` executa a geração incremental por uma sessão fornecida
  pelo chamador.
- Temperatura configurável, Top-K configurável (zero desativa), softmax estável,
  EOS como parada e bloqueio de PAD/BOS/USR/BOT como conteúdo.
- Limites de entrada e saída, timeout, cancelamento e política para resposta
  vazia são explícitos.
- A saída passa pelo `ByteTokenizer`, com validação UTF-8 e política de
  substituição/rejeição configurável.
- O estado da sessão continua pertencendo ao chamador; cada chamada exige seu
  próprio buffer de estado.

### 10.2 Quantização INT8

- `quantization.hpp/.cpp` implementa quantização simétrica por linha para
  valores INT8 no intervalo [-127, 127], com uma escala FP32 por linha.
- Linhas zeradas usam escala 1.0; NaN, infinito, dimensões inconsistentes e
  escalas inválidas são rejeitados.
- A estrutura quantizada é armazenada uma vez. A futura GRU deverá referenciar
  a mesma estrutura para lookup de embedding e projeção de saída (weight tying),
  sem criar cópias sincronizadas.

### 10.3 Artefato .morok

- `model_artifact.hpp/.cpp` define cabeçalho versionado de 64 bytes, campos
  little-endian explícitos, identidade G3/arquitetura, precisão, dimensões,
  estado, contagem real de parâmetros, tamanho do payload, escalas e CRC32.
- `save_model_artifact_atomic` grava em arquivo temporário, relê e valida o
  artefato antes do rename final; falha de escrita/validação/rename preserva o
  destino anterior. `read_model_artifact` limita o tamanho antes de alocar.
- O carregador rejeita magic/versão/arquitetura incompatíveis, dimensões fora
  dos limites, payload truncado/excessivo, layout de precisão incorreto,
  bytes reservados não nulos e checksum inválido.
- FP32 exige exatamente quatro bytes por parâmetro. INT8 exige um byte por
  parâmetro mais quatro bytes por escala.
- A contagem é informada pela estrutura real; 198.149 é referência da
  especificação, não valor forçado pelo serializador.

### 10.4 Medição e isolamento

- `benchmark.hpp/.cpp` mede latências por execução, mediana, p95,
  throughput de bytes, tamanho do estado por sessão, RSS antes/depois e pico
  de RSS quando o sistema operacional expõe esses dados.
- O benchmark recebe um backend real como argumento; os testes atuais usam
  backend determinístico de fixture apenas para validar o instrumento.
- Testes executam oito sessões em paralelo com backend compartilhado e estados
  independentes.

### 10.5 Estado de validação

- [x] Decodificação Top-K/temperatura, EOS e filtros de tokens de controle.
- [x] Limites, timeout/cancelamento e política de resposta vazia.
- [x] Quantização/dequantização INT8 simétrica por linha.
- [x] Serialização/leitura do artefato e validação CRC32/compatibilidade.
- [x] Testes de corrupção, layout FP32/INT8 e isolamento de sessões.
- [x] Harness de benchmark com mediana, p95, throughput e memória residente.
- [ ] Equivalência FP32/INT8 medida com pesos reais da GRU G3.
- [ ] RAM e latência de produção medidas em hardware de referência.
- [ ] Weight tying integrado ao armazenamento real da GRU G3.
- [ ] Teste de concorrência com o backend neural G3 real.

Os itens pendentes dependem da implementação do backend neural e dos pesos SFT
G3. Fixtures sintéticas validam os contratos e a infraestrutura, não a
qualidade nem o desempenho do modelo final.
