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
