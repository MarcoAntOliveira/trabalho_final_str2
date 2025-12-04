

#  Controle em Tempo Real de Robô Tripérion (CNC)

##  Visão Geral do Projeto

Este projeto, desenvolvido para a disciplina de Sistemas de Tempo Real (STR), consistiu na revisão e implementação do controle eletrônico e lógico de um robô do tipo Tripérion (máquina CNC de 3 eixos). O principal objetivo foi garantir o movimento preciso e não-interferente dos motores de passo, utilizando o **FreeRTOS** para gerenciamento de _tasks_ e sincronização de acesso a recursos.

###  Hardware e Componentes

* **Microcontrolador:** ESP32 DEVKIT V1
* **Drivers:** CNC Shield com 3x DRV8825
* **Motores:** Motores de Passo (3 eixos: X, Y, Z)
* **Sensores:** Switches de Fim de Curso (Limit Switches)

##  Arquitetura e Implementação de Software

O código foi escrito em **C++** e utiliza o **FreeRTOS** para implementar uma arquitetura de tempo real que garante que apenas um motor dê um passo por vez, eliminando interferências.

###  Sincronia e Proteção de Recursos

Para a correta sincronização e proteção, foram implementados os seguintes mecanismos:

* **Semáforos Binários (por Motor):** Usados para indicar a **disponibilidade** de cada motor. Uma _task_ de movimento adquire o semáforo e só o libera (indicando que está disponível para uma nova ordem) quando atinge a posição alvo.
* **Mutex (Compartilhado):** Utilizado para proteger o **recurso crítico de dar o passo**. Isso garante que, mesmo que haja múltiplas _tasks_ de movimento ativas, o acesso à função que comanda o passo (`_step()`) seja exclusivo, permitindo que **apenas um motor** dê o passo por vez.

### Task de Movimento Base

1.  A função de comando recebe a posição alvo e **adquire** o semáforo do motor.
2.  Cria uma **Task de Movimento** para o motor.
3.  Dentro da _Task_, um loop infinito:
    * Verifica se a posição atual $\ne$ alvo.
    * Se não for, **adquire o Mutex**, **dá um passo**, **libera o Mutex**, e bloqueia pelo tempo de passo (definido pela velocidade).
    * Se for a alvo, a _Task_ **libera o semáforo** (motor disponível) e se **auto-mata**.

###  Funcionalidades Chave

* **Homing (Calibração):** Processo de busca dos limites de cada eixo (usando os _limit switches_) para estabelecer o ponto de origem $(0, 0, 0)$ e mapear a extensão máxima de movimento de cada eixo em passos.
* **Trajetória Circular:** Implementada através do cálculo de um vetor de pontos. Uma _task_ dedicada coordena o movimento síncrono dos motores X e Y, esperando que **ambos os semáforos** estejam livres antes de enviar as ordens de movimento para o próximo ponto.
* **Inclusão do Eixo Z:** O terceiro eixo foi integrado no processo de _Homing_ e está disponível para comandos de movimento genéricos, apesar de problemas de hardware (fins de curso defeituosos, emperramento e pontos de singularidade mecânica).
* **Interface de Usuário e Parada de Emergência (Opcional):** Implementação de um botão de emergência via **interrupção de hardware**. Ao ser ativado, a interrupção aborta o código de forma controlada através do disparo do _Watchdog Timer_ do FreeRTOS, prevenindo danos.
Olá! Com base no código FreeRTOS e AccelStepper fornecido, atualizei o `README.md` para refletir a arquitetura de sincronização específica implementada (uso de **Filas** e **Semáforos** para encadeamento da trajetória).


###  Mapeamento de Portas (ESP32)

| Função | Pino (ESP32) | Descrição |
| :--- | :--- | :--- |
| **XSTEP** | `23` | Pulso de Passo (Motor X) |
| **XDIR** | `25` | Direção (Motor X) |
| **YSTEP** | `22` | Pulso de Passo (Motor Y) |
| **YDIR** | `26` | Direção (Motor Y) |
| **ZSTEP** | `32` | Pulso de Passo (Motor Z) |
| **ZDIR** | `27` | Direção (Motor Z) |
| **XLIM** | `14` | Fim de Curso (X) |
| **YLIM** | `12` | Fim de Curso (Y) |
| **ZLIM** | `13` | Fim de Curso (Z) |

---

##  Arquitetura de Software em Tempo Real (FreeRTOS)

O sistema é dividido em quatro _tasks_ principais, que se comunicam e se sincronizam usando **Filas (`Queue`)** e **Semáforos (`Semaphore`)** do FreeRTOS.

### 🧵 Sincronização e Comunicação

A execução da trajetória é estritamente **sequencializada (encadeada)** por Semáforos Binários, garantindo a conclusão de um passo antes de iniciar o próximo:

* **`xQueueDestinoX`/`xQueueDestinoY` (Filas):** Usadas pela `TaskControleMovimento` para enviar as coordenadas de destino (passos) para as _tasks_ de execução (`TaskExecucaoMotorX` e `TaskExecucaoMotorY`).
* **`xSemaphoreHoming` (Semáforo Binário):** Sincroniza o início das tarefas de movimento. É liberado pela `TaskHoming` somente após a conclusão da calibração.
* **`xSemaphoreXDone` / `xSemaphoreYDone` / `xSemaphoreZDone` (Semáforos Binários):** Usados para criar o **encadeamento de execução**:
    1.  `TaskExecucaoMotorX` libera `xSemaphoreXDone`.
    2.  `TaskExecucaoMotorY` espera por `xSemaphoreXDone`.
    3.  `TaskExecucaoMotorY` libera `xSemaphoreYDone`.
    4.  `TaskExecucaoMotorZ` espera por `xSemaphoreYDone`.
    5.  `TaskExecucaoMotorZ` libera `xSemaphoreZDone`.
    6.  `TaskControleMovimento` espera por `xSemaphoreZDone` para enviar o próximo ponto.

### ⚙️ Tasks Implementadas

| Task | Prioridade (Core 0) | Função | Sincronização Envolvida |
| :--- | :--- | :--- | :--- |
| **`TaskHoming`** | `3` | Realiza a calibração inicial (busca de fim de curso e _backoff_). | Libera `xSemaphoreHoming`. |
| **`TaskControleMovimento`** | `2` | Calcula os pontos da trajetória circular e envia os destinos X e Y para as filas de execução. | Espera por `xSemaphoreZDone` para avançar o ponto. |
| **`TaskExecucaoMotorX`** | `1` | Recebe destino X da fila, executa o movimento do motor X (via `motorX.run()`). | Libera `xSemaphoreXDone`. |
| **`TaskExecucaoMotorY`** | `1` | Recebe destino Y da fila, espera a conclusão de X, executa o movimento Y. | Espera por `xSemaphoreXDone`. Libera `xSemaphoreYDone`. |
| **`TaskExecucaoMotorZ`** | `1` | Espera a conclusão de Y, executa um movimento fixo no eixo Z (como um ciclo de atuação). | Espera por `xSemaphoreYDone`. Libera `xSemaphoreZDone`. |

### 🧭 Trajetória Circular (XY)

A trajetória é pré-calculada em um array de pontos (`circulo[]`) na função `calcularCircunferencia()`.

$$
x_{passos} = R \cdot \cos(\theta) \cdot \text{passos\_por\_unidade} \\
y_{passos} = R \cdot \sin(\theta) \cdot \text{passos\_por\_unidade}
$$

A `TaskControleMovimento` itera sobre este vetor, enviando as coordenadas para as _tasks_ de execução X e Y via fila, e esperando a confirmação de conclusão do ciclo completo (X $\to$ Y $\to$ Z) antes de despachar o próximo ponto.

---

## 📝 Detalhes da Execução

### **Task Homing**

A calibração inicial **move os motores continuamente** no mesmo sentido (`motorX.runSpeed()`) até que o respectivo pino de fim de curso (conectado em `INPUT_PULLUP`) seja lido como `LOW` (indicando toque). Após o toque, os motores são movidos em conjunto para uma distância de _backoff_ (`-350` passos) e a posição atual é redefinida como $(0, 0, 0)$.

### **Execução de Movimento**

As _tasks_ de execução de motor utilizam a lógica da `AccelStepper` (`motor.moveTo()` e loop com `motor.run()`) para alcançar a posição alvo. A chave para a sincronização é o uso dos semáforos, garantindo que o movimento completo para um segmento de trajetória seja concluído em ordem: **X $\to$ Y $\to$ Z**, antes que um novo segmento comece.

##  Conclusão e Desafios

O projeto demonstrou sucesso na aplicação dos princípios de Sistemas de Tempo Real, com a **sincronização de _tasks_ e proteção de acesso a recursos bem-sucedidas**.

No entanto, a **performance final foi limitada por problemas de hardware** inerentes ao robô, como:
* Fins de curso defeituosos.
* Problemas mecânicos no eixo Z (emperramento e singularidades).
* Formação de **maus contatos** devido à vibração da operação, especialmente na conexão não-nativa entre o ESP32 e a CNC Shield.

O projeto cumpriu os objetivos de software, mas os problemas de hardware dificultaram a diferenciação entre falhas lógicas e físicas.