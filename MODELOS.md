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

## Organização real do código

O diretório público `include/` e a biblioteca `Morok::Core` contêm contratos e infraestrutura neutra, usados por mais de uma geração. Não devem conter parâmetros, forward pass, treinamento, geração, checkpoints ou adaptadores específicos de uma geração/modelo.

A implementação compartilhada da geração G1 está em `G1/shared/` e é compilada apenas em `Morok::G1`. Inclui o motor de parâmetros, forward pass, BPTT, otimizador, treinamento, inferência, checkpoints e adaptadores de linguagem/comunicação/aprendizado que ainda são específicos da G1.

A implementação da G2 está dentro de `G2/`. `G2/engine/` contém o motor independente por especialista; `G2/legacy/` mantém os adaptadores da G2 que compõem os dois especialistas. A G2 não importa headers nem símbolos da G1. Os alvos `Morok::G1` e `Morok::G2` dependem do Core, mas não dependem um do outro.

Aplicações, scripts, dados de reforço, fixtures e testes específicos foram movidos para `G1/` ou `G2/`. O CMake continua na raiz como orquestrador de build e só inclui fontes da geração correspondente no alvo correspondente.

## Estado dos perfis

As pastas `G1/{Mokosh,Tsukuyomi,Artemis,Apollo}` e `G2/{Mokosh,Tsukuyomi,Artemis,Apollo}` ainda contêm especificações iniciais, não implementações completas e independentes. Os motores atuais são motores compartilhados no nível da geração; ainda faltam os adaptadores, políticas e alvos independentes de cada perfil. A existência de uma pasta ou README não significa que o perfil esteja funcional.

G3 ainda não possui motor executável, treinamento ou inferência. Seus diretórios são apenas estrutura documental. Não há dependência de G1 ou G2 dentro da G3.

A migração física da base G1/G2 foi feita, mas a migração total só pode ser considerada concluída depois de implementar os perfis, eliminar todos os caminhos históricos restantes, e obter CI, testes, sanitizers, fuzzing e workflows de treinamento/reforço verdes no commit final.

## Modelo removido
Hephaestus não faz parte da arquitetura aprovada.
