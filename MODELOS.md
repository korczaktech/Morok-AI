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

## Estado da migração

Esta especificação e a estrutura inicial de diretórios estabelecem a organização de destino. O código C++ atual da G1 e G2 permanece nas pastas históricas `src/`, `include/`, `apps/` e `tests/`. Ele ainda não foi atribuído a Mokosh, Tsukuyomi, Artemis ou Apollo. Não movi esses arquivos sem esse mapeamento, pois isso poderia atribuir incorretamente a arquitetura atual e quebrar workflows, testes e checkpoints.

A próxima etapa técnica é inventariar dependências, separar bibliotecas e alvos CMake e, então, migrar o código para o modelo correto. Até lá, os novos diretórios são estrutura organizacional e especificação, não implementações executáveis independentes.

## Modelo removido
Hephaestus não faz parte da arquitetura aprovada.
