# Teste de Carga — Breakpoint Test

A fim de identificar o ponto de ruptura do sistema da [Ponderada 2](https://github.com/SophiSenne/arquitetura-fila), foram realizados mais testes, escalando a carga progressivamente até que falhas observáveis ocorressem.

Como complemento aos testes de carga já existentes na Ponderada 2, foi adicionado o `breakpoint_test.ts` com o propósito de encontrar o limite operacional da aplicação, ou seja, descobrir até qual patamar de requisições simultâneas o sistema se comporta de forma aceitável antes de começar a falhar.

## Cenário de Teste

O teste simula múltiplos sensores IoT enviando leituras via HTTP POST para o endpoint `/sensorData` da API Go (`go_backend:8080`). Os dados são enfileirados no RabbitMQ pelo backend.

### Estágios de Carga

| Estágio | Duração | VUs (alvo) | Descrição                    |
|---------|---------|------------|------------------------------|
| 1       | 0,3 min | 50         | Warm-up inicial              |
| 2       | 1 min   | 500        | Carga baixa                  |
| 3       | 1 min   | 1.000      | Carga moderada               |
| 4       | 1 min   | 2.000      | Carga alta                   |
| 5       | 1 min   | 3.000      | Carga muito alta             |
| 6       | 1 min   | 5.000      | Pico máximo                  |
| 7       | 2 min   | 5.000      | Sustentação no pico          |
| 8       | 1 min   | 0          | Rampa de descida (cool-down) |

**Duração total:** ~8,3 minutos

### Thresholds (critérios de aceitação)

| Métrica                    | Limite         |
|----------------------------|----------------|
| `http_req_duration` p(95)  | < 5.000 ms     |
| `sensor_error_rate`        | < 20% de erros |
| `sensor_request_duration`  | mediana < 2 s  |

## Métricas Coletadas

| Métrica                    | Tipo    | Descrição                                         |
|----------------------------|---------|---------------------------------------------------|
| `sensor_error_rate`        | Rate    | Taxa de requisições que falharam nos checks       |
| `sensor_request_duration`  | Trend   | Duração de cada requisição (em ms)                |
| `sensor_total_requests`    | Counter | Total de requisições enviadas                     |
| `rabbitmq_publish_errors`  | Counter | Respostas HTTP 500 (falha de publicação no MQ)    |
| `timeout_errors`           | Counter | Respostas com status 0 (timeout)                  |

## Resultados

### Execução com recursos padrão

Com a configuração padrão de CPU da API Go (sem restrições), nenhuma falha foi observada e todos os thresholds foram respeitados mesmo no pico de 5.000 VUs simultâneos.

### Execução com CPU limitada

Para forçar o breakpoint, a CPU disponível para a API foi progressivamente reduzida:

| CPU disponível | Erros de latência | Mensagens perdidas no RabbitMQ |
|:--------------:|:-----------------:|:------------------------------:|
| 0,50 vCPU      | Não observados    | 0                              |
| 0,30 vCPU      | Observados        | 0            |
| 0,25 vCPU      | Observados        | 67 mensagens               |

> **Breakpoint identificado:** com 0,25 vCPU, o sistema começou a apresentar erros de latência e 67 mensagens não foram entregues à fila do RabbitMQ, este sendo, portanto, o possível ponto de ruptura sob a carga gerada pelo teste.

