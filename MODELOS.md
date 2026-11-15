# Especificação dos modelos Morok AI

## Organização por geração

Cada geração contém quatro modelos especializados. G1, G2 e G3 são gerações; Mokosh, Tsukuyomi, Artemis e Apollo são modelos dentro de cada geração.

```text
Morok-AI/
├── G1/{Mokosh,Tsukuyomi,Artemis,Apollo}/
├── G2/{Mokosh,Tsukuyomi,Artemis,Apollo}/
├── G3/{Mokosh,Tsukuyomi,Artemis,Apollo}/
├── core/
├── tools/
└── ...
```

As pastas públicas da raiz abrigam somente capacidades realmente compartilhadas. Código exclusivo, parâmetros, configurações, memória especializada, agentes, treinamento, inferência e testes pertencem ao respectivo modelo, conforme a necessidade real. Não duplicar código público dentro das gerações.

## Modelos

### Mokosh — eficiência e baixo consumo
Modelo orientado a uma execução mais calma e econômica. Prioriza reduzir memória, processamento e energia, mantendo qualidade suficiente para a tarefa. Deve evitar agentes e operações adicionais sem benefício mensurável.

### Tsukuyomi ⚫ — agente único
Modelo de identidade visual escura/preta. Opera com exatamente um agente. Não delega tarefas a agentes adicionais nem forma equipes de agentes; prioriza execução individual e autocontida.

### Artemis — arquitetura multiagente
Modelo destinado a tarefas que se beneficiam de colaboração. Deve utilizar mais de dois agentes, portanto no mínimo três agentes ativos na arquitetura de execução. Coordenação, divisão de tarefas e consolidação de resultados pertencem à implementação exclusiva de Artemis.

### Apollo — baixa latência
Modelo orientado à velocidade de resposta. Prioriza reduzir o tempo até a primeira resposta e o tempo total de inferência. Pode usar mais recursos quando isso produzir redução de latência medida e reproduzível.

## Regras de isolamento

1. Código exclusivo de um modelo fica dentro da pasta desse modelo e não pode ser importado por outro.
2. Um modelo não depende de diretórios privados de outra geração ou modelo.
3. Componentes reutilizáveis ficam nas bibliotecas públicas da raiz, como `core/` e `tools/`, ou em outras bibliotecas públicas explicitamente aprovadas.
4. Uma biblioteca pública não pode conter lógica específica de Mokosh, Tsukuyomi, Artemis ou Apollo.
5. Cada modelo mantém seus próprios parâmetros, configuração, inferência, treinamento, memória especializada, agentes e testes quando necessários.
6. Não criar diretórios ou subsistemas que o modelo não utiliza.
7. O CMake deve oferecer alvos independentes por modelo. Compilar um modelo não compila fontes privadas dos demais.
8. Testes de isolamento verificam que os alvos não incluem fontes privadas de outros modelos.
9. Checkpoints e artefatos identificam geração e modelo; compartilhamento exige conversor explícito, versionado e testado.
10. Evoluções posteriores não podem obrigar gerações anteriores a importar funcionalidades mais avançadas.

## Relação entre gerações

Cada geração implementará os quatro perfis. Mokosh mantém a prioridade de eficiência; Tsukuyomi usa exatamente um agente; Artemis usa mais de dois agentes; Apollo prioriza baixa latência. A quantidade de parâmetros, mecanismos cognitivos e capacidades de cada geração será definida em especificação própria. Modelos homônimos de gerações diferentes não precisam ter a mesma arquitetura interna.

## Separação de código e dependências

O `core/` público contém infraestrutura compartilhada. As bibliotecas de geração são alvos separados: `Morok::G1` e `Morok::G2`. G1 e G2 podem depender do core, mas não podem importar headers, tipos, parâmetros, treinamento, inferência ou checkpoints privados um do outro.

A implementação da G2 foi desmembrada em `G2/engine/`. Ela possui inventário de parâmetros, estado recorrente, forward pass, BPTT, otimizador, checkpoint, treinamento e geração próprios. Os dois especialistas internos da G2 usam esse motor privado da própria geração. Não são instâncias da implementação da G1.

A implementação da G1 está isolada no alvo `Morok::G1`, separado do alvo público `Morok::Core`. Os executáveis e testes de G1 vinculam a biblioteca G1. G2 vincula apenas `Morok::G2` e `Morok::Core`. A separação é reforçada por um teste estático que impede referências da G2 a símbolos ou headers privados da G1.

## Estado da migração

A separação entre os motores de G1 e G2 e o desacoplamento da G2 são etapas implementadas nesta revisão. Ainda há arquivos históricos de G1 em `src/` e `include/`; a próxima migração física deve movê-los para `G1/engine/` e atualizar seus consumidores sem deixar headers privados na camada pública.

Os diretórios `G1/`, `G2/` e `G3/` contêm os perfis nomeados, mas isso não significa que os quatro modelos de cada geração já tenham quatro motores executáveis independentes. Mokosh, Tsukuyomi, Artemis e Apollo ainda precisam de suas políticas e implementações exclusivas integradas aos respectivos motores. G3 permanece sem motor implementado. A documentação não deve ser interpretada como prova de implementação.

## Modelo removido
Hephaestus não faz parte da arquitetura aprovada.
