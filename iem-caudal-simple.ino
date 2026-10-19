#include <Wire.h>
#include <LiquidCrystal_I2C.h>

//=========================================================
//DEFINICION DE LOS PINES
//Pines para el encoder rotatorio
#define CLK 4
#define DT 16
#define SW 17 //pin de entrada
//Pines de salida de la maquina
#define VERDE 25
#define ROJO 26
#define BOMBA 32
#define VENT 33
#define	BUZZER 18
#define NIVEL 14 //pin de entrada nivel
#define CAUDAL 5 //pin de entrada para el caudalimetro 
//Para el Caudalimetro
#define VOL_PULSOS 0.5f
#define PULSOS 75.0f
#define TIEMPO_ACTUALIZAR_CAUDAL 850 //[ms]
//Para el Nivel de Agua
#define NIVEL_DEBOUNCE 5000 //[ms]

//Para el display LCD
#define SCL 22
#define SDA 21
LiquidCrystal_I2C lcd(0x27, 20, 4);


//=========================================================
//VARIABLES GLOBALES
//Para el encoder rotatorio
bool estadoActualCLK = false;
bool estadoUltimoCLK = false;
volatile int hora = 0, minuto = 0, segundo = 0;
volatile int hora_inicial = 0, minuto_inicial = 0, segundo_inicial = 0;
//Para el boton del encoder rotatorio
bool botonPresionado = false;
bool botonPresionadoLargo = false;
bool botonPresionadoDoble = false;
//const int BOTON_DEBOUNCE = 50; //ms, por el momento no esta usado
const int PRESIONADO_LARGO = 1000; //ms
const int LIM_ENTRE_DOBLE_PRES = 350; //ms
uint32_t botonPresInicio = 0;
uint32_t botonTiempoPres = 0;
uint32_t botonPresFinal = 0;
uint32_t botonTiempoEntrePres = 0;
//Variables globales para el sistema
uint32_t lecturaTiempoNivel = 0; //debounce nivel
volatile int indicePantallaOriginalNivel;
volatile bool estadoSistema = false;
volatile int pantallaAnteriorOriginal;
volatile bool pantallas[9] = {false};
volatile int tamagnoPantallas = (int) (sizeof(pantallas) / sizeof(pantallas[0]));
//Variables para el Caudalimetro
volatile float volumenAgua = 0.0f;
volatile float caudalAguaInstante = 0.0f;
volatile int cantidadPulsos = 0;
volatile float sumCaudalProm = 0.0f;
volatile unsigned long int contadorCaudal = 0;

//Simbolos personalizados para la pantalla LCD
byte simboloPlay[8] = {0x1F, 0x17, 0x13, 0x11, 0x13, 0x17, 0x1F, 0x00};
byte simboloPausa[8] = {0x1F, 0x15, 0x15, 0x15, 0x15, 0x15, 0x1F, 0x00};
byte simboloParo[8] = {0x1F, 0x1F, 0x11, 0x11, 0x11, 0x1F, 0x1F, 0x00};
byte simboloFlecha[8] = {0x08, 0x0C, 0x0E, 0x0F, 0x0E, 0x0C, 0x08, 0x00};
byte simboloInterrogacionInicial[8] = {0x04, 0x00, 0x00, 0x04, 0x04, 0x04, 0x04, 0x00};

//=========================================================
//PROTOTIPOS DE FUNCIONES
int obtenerIndicePantalla();
void gestorPantallas(int pantalla);
void dibujarCaracteristicas(int hora, int minuto, int segundo, int pantalla);
void dibujarCaracteristicasTiempo(int tipo);
void intercambioRoles(volatile bool arreglo[], volatile int tamagno_arreglo, int indice);
void reiniciarValoresHMS();
void reiniciarValoresVolAgua();
void iniciarSistema();
void pausarSistema();
void detenerSistema();
void alertaSistema(bool opcion);
void parpadeoVerde(TimerHandle_t xTimer);
void parpadeoPausaFin(TimerHandle_t xTimer);
void cuentaRegresiva(TimerHandle_t xTimer);
void IRAM_ATTR notificarCaudal();
void sonidoPulsacionCorta();
void sonidoConfirmar();
void sonidoPulsacionLarga();
void sonidoFinalizacion();
void sonidoNivelBajo();

//=========================================================
//FREERTOS	
//********************************************************
//Handle de los Temporizadores 
TimerHandle_t ParpadeoVerdeTimer = NULL;
TimerHandle_t CuentaRegresivaTimer = NULL;
TimerHandle_t ParpadeoPausaFinTimer = NULL;

//********************************************************
//Handle de Semaforos 
SemaphoreHandle_t EstadoSistemaMutex = NULL;
SemaphoreHandle_t ArrayPantallasMutex = NULL;
SemaphoreHandle_t HoraMinSegMutex = NULL;
SemaphoreHandle_t HoraMinSegInicialMutex = NULL;
SemaphoreHandle_t VolumenAguaMutex = NULL;
SemaphoreHandle_t CaudalAguaInstanteMutex = NULL;
SemaphoreHandle_t CantidadPulsosMutex = NULL;
SemaphoreHandle_t PantallaAntOriginalMutex = NULL;
SemaphoreHandle_t DisplayMutex = NULL; //mutex para el lcd
SemaphoreHandle_t SonidoMutex = NULL; //para asegurar el inicio y fin de los sonidos
SemaphoreHandle_t PulsoCaudalSemaphoreBin = NULL; //semaforo binario

//********************************************************
//Handle de las Tareas
TaskHandle_t EncoderRotatorioTaskHandle = NULL;
TaskHandle_t EncoderBotonTaskHandle = NULL;
TaskHandle_t PulsacionCortaTaskHandle = NULL;
TaskHandle_t PulsacionDobleTaskHandle = NULL;
TaskHandle_t PulsacionLargaTaskHandle = NULL;
TaskHandle_t ServidorGraficoTaskHandle = NULL;
TaskHandle_t CompositorGraficoTaskHandle = NULL;
TaskHandle_t CuentaRegresivaTaskHandle = NULL;
TaskHandle_t SupervisorNivelAguaTaskHandle = NULL;
TaskHandle_t GestorVolAguaTaskHandle = NULL;
TaskHandle_t GestorCaudalAguaTaskHandle = NULL;
TaskHandle_t GestorSonidosTaskHandle = NULL;
TaskHandle_t IntercambioPantallasTaskHandle = NULL;

//********************************************************
//Tareas FreeRTOS
//Tarea 1. Encoder Rotatorio
void EncoderRotatorioTask(void *parameter){
	for (;;){
		estadoActualCLK = digitalRead(CLK) ? true : false;

		if ((estadoActualCLK != estadoUltimoCLK) && estadoActualCLK == false){
			bool estadoDT = digitalRead(DT) ? true : false;
			//Horario
			if (estadoDT == true){
				minuto++; 
				if (minuto > 59){
					minuto = 0;
					hora++;
					if (hora > 23) hora = 0;
				}
			}
			//Antihorario
			else{
				minuto--;
				if (minuto < 0){
					minuto = 59;
					hora--;
					if (hora < 0) hora = 23;
				}
			}
			//Actualizar Pantalla
			xTaskNotifyGive(CompositorGraficoTaskHandle);

			Serial.printf("hora: %d\n", hora);
			Serial.printf("Minuto: %d\n", minuto);
		}
		estadoUltimoCLK = estadoActualCLK;

		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}

//Tarea 2. Boton del Encoder Rotatorio
void EncoderBotonTask(void *parameter){
	for (;;){
		bool estadoBotonReal = digitalRead(SW) ? false : true; //ya que es pullup

		//detectando flando de bajada
		if (estadoBotonReal && !botonPresionado){
			botonPresionado = true;
			botonPresInicio = millis();
			botonPresionadoLargo = false;
			botonPresionadoDoble = false;
		}

		//detectando pulsacion larga
		botonTiempoPres = millis() - botonPresInicio;
		if (estadoBotonReal && botonPresionado && !botonPresionadoLargo && (botonTiempoPres > PRESIONADO_LARGO)){
			botonPresionadoLargo = true;
			xTaskNotifyGive(PulsacionLargaTaskHandle); //PULSACION LARGA
		}

		//detectando flando de subida
		if (!estadoBotonReal && botonPresionado){
			botonPresionado = false;
			botonPresFinal = millis();
			botonTiempoEntrePres = millis() - botonPresFinal;
			while (botonTiempoEntrePres <= LIM_ENTRE_DOBLE_PRES){
				//detectando flanco de bajada para la segunda pulsacion
				estadoBotonReal = digitalRead(SW) ? false : true; //ya que es pullup

				//detectando flando de bajada
				if (estadoBotonReal && !botonPresionado){
					botonPresionado = true;
					botonPresInicio = millis();
				}
				if (!estadoBotonReal && botonPresionado){
					botonPresionado = false;
					botonPresionadoDoble = true;
					xTaskNotifyGive(PulsacionDobleTaskHandle); //PULSACION DOBLE
					break; //saliendo del bucle while
				}

				botonTiempoEntrePres = millis() - botonPresFinal;
				vTaskDelay(2 / portTICK_PERIOD_MS);
			}

			if (!botonPresionadoLargo && !botonPresionadoDoble){
				xTaskNotifyGive(PulsacionCortaTaskHandle); //PULSACON CORTA
			}
		}

		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}
 
//Tarea 3. Pulsacion Corta
void PulsacionCortaTask(void *parameter){
	for(;;){
		//Espera infinita la notificacion de la pulsacion corta
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		sonidoPulsacionCorta();
		Serial.println("Pulsacion CORTA");

		bool estadoSistema_LocalPC = false;
		//obteniendo el estado del sistema de la variable global
		if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			estadoSistema_LocalPC = estadoSistema;
			xSemaphoreGive(EstadoSistemaMutex);
		}

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos
		indicePantalla = obtenerIndicePantalla();
	
		int hora_LocalPC, minuto_LocalPC, segundo_LocalPC;
		switch (indicePantalla){
			case 1: //Ajustar Tiempo => Iniciar Sistema
				//copiando las variables de tiempo global a local
				if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(5)) == pdTRUE){
					hora_LocalPC = hora;	
					minuto_LocalPC = minuto;
					segundo_LocalPC = segundo;
					
					xSemaphoreGive(HoraMinSegMutex);
				}

				//verificando si el tiempo configurado es distinto de cero
				if ((hora_LocalPC != 0 || minuto_LocalPC != 0 || segundo_LocalPC != 0) && estadoSistema_LocalPC == false){
					estadoSistema_LocalPC = true; //estado general del sistema

					//guardando los valores iniciales del tiempo para posterior muestra
					if (xSemaphoreTake(HoraMinSegInicialMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						hora_inicial = hora_LocalPC;
						minuto_inicial = minuto_LocalPC;
						segundo_inicial = segundo_LocalPC;

						xSemaphoreGive(HoraMinSegInicialMutex);
					}

					iniciarSistema();	
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, 2);

						xSemaphoreGive(ArrayPantallasMutex);
					}

					//detener la tarea del encoder rotatorio (perilla)
					vTaskSuspend(EncoderRotatorioTaskHandle);
					vTaskResume(GestorVolAguaTaskHandle);
					vTaskResume(GestorCaudalAguaTaskHandle);
					
					//actualizar pantalla
					xTaskNotifyGive(ServidorGraficoTaskHandle);
					vTaskDelay(5 / portTICK_PERIOD_MS); //esperar un tiempo y actualizar los numeros de inicio
					xTaskNotifyGive(CompositorGraficoTaskHandle); 

					//iniciar y detener temporizadores
					xTimerStart(CuentaRegresivaTimer, 1000); //esperar un segundo para iniciar el temporizador
					xTimerStop(ParpadeoVerdeTimer, 0); //0: tiempo de espera para el stop (redundante)
					xTimerStop(ParpadeoPausaFinTimer, 0);
					sonidoConfirmar();
					
					//actualizar el estado del sistema en la variable global
					if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
						estadoSistema = estadoSistema_LocalPC;
						Serial.printf("Dentro de 1 PC: %d\n", estadoSistema);
						
						xSemaphoreGive(EstadoSistemaMutex);
					}
					
				}
				break;

			case 2: case 7: //Sistema en Funcionamiento (al presionar aqui no cambina el estadoSistema) => Pausar el Sistema
				pausarSistema();
				if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					intercambioRoles(pantallas, tamagnoPantallas, 3); //pantalla de pausa
					xSemaphoreGive(ArrayPantallasMutex);
				}

				vTaskSuspend(GestorVolAguaTaskHandle);
				vTaskSuspend(GestorCaudalAguaTaskHandle);
				vTaskResume(IntercambioPantallasTaskHandle);
				vTaskResume(GestorSonidosTaskHandle);
				
				//actualizar pantalla
				xTaskNotifyGive(ServidorGraficoTaskHandle);

				//gestion de los timers
				//xTimerStart(ParpadeoVerdeTimer, 0); //0: tiempo de espera para el inicio
				xTimerStart(ParpadeoPausaFinTimer, 0); //temporizdor para el intercambio de pantallas
				xTimerStop(CuentaRegresivaTimer, 0);

				//Guardar el indice de pantalla siguiente | original (para el intercambiador de pantallas)
				if (xSemaphoreTake(PantallaAntOriginalMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					pantallaAnteriorOriginal = 3;

					xSemaphoreGive(PantallaAntOriginalMutex);
				}
				Serial.printf("Dentro de 2 PC: %d\n", estadoSistema_LocalPC);

				break;

			case 3: case 5: //Sistema en Pausa (al presionar aqui no cambia el estado del sistema) => Reanudar Sistema
				if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(5)) == pdTRUE){
					hora_LocalPC = hora;	
					minuto_LocalPC = minuto;
					segundo_LocalPC = segundo;
					
					xSemaphoreGive(HoraMinSegMutex);
				}

				//verificar que el tiempo sea distinto de cero para evitar inconvenientes
				if ((hora_LocalPC != 0 || minuto_LocalPC != 0 || segundo_LocalPC != 0) && estadoSistema_LocalPC == true){
					iniciarSistema();	
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, 2);
						xSemaphoreGive(ArrayPantallasMutex);
					}

					//actualizar pantalla
					xTaskNotifyGive(ServidorGraficoTaskHandle);
					vTaskDelay(5 / portTICK_PERIOD_MS); //esperar un tiempo y actualizar los numeros de inicio
					xTaskNotifyGive(CompositorGraficoTaskHandle); 

					//gestionar los temporizadores
					xTimerStart(CuentaRegresivaTimer, 0);
					xTimerStop(ParpadeoPausaFinTimer, 0);	
					//xTimerStop(ParpadeoVerdeTimer, 0); //0: tiempo de espera para el stop (redundante)

					//gestion de las tareas
					vTaskSuspend(IntercambioPantallasTaskHandle);
					vTaskSuspend(GestorSonidosTaskHandle);
					vTaskResume(GestorVolAguaTaskHandle);
					vTaskResume(GestorCaudalAguaTaskHandle);
				}
				else { //de lo contrario se lleva a un estado de finalizado

				}
				Serial.printf("Dentro de 3 PC: %d\n", estadoSistema_LocalPC);

				break;

			case 4: case 6://Funcionamiento Finalizado => Pantalla de configuracion de temporizador
				if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					intercambioRoles(pantallas, tamagnoPantallas, 1);
					xSemaphoreGive(ArrayPantallasMutex);
				}

				//actualizar pantalla
				xTaskNotifyGive(ServidorGraficoTaskHandle);

				//gestion de los timers
				xTimerStop(ParpadeoPausaFinTimer, 0);

				//gestion de las tareas
				vTaskResume(EncoderRotatorioTaskHandle);
				vTaskSuspend(IntercambioPantallasTaskHandle);
				vTaskSuspend(GestorSonidosTaskHandle);

				reiniciarValoresVolAgua(); //se incluye el caudal promedio
				Serial.printf("Dentro de 4 PC: %d\n", estadoSistema_LocalPC);

				break;
		}

		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}

//Tarea 3.1 Pulsacion Doble
void PulsacionDobleTask(void *parameter){
	for (;;){
		//Espera infinita la notificacion de la pulsacion larga
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		Serial.println("Pulsacion DOBLE");

		bool estadoSistema_LocalPD = false;
		//obteniendo el estado del sistema de la variable global
		if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			estadoSistema_LocalPD = estadoSistema;
			xSemaphoreGive(EstadoSistemaMutex);
		}

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos
		indicePantalla = obtenerIndicePantalla();
	
		switch (indicePantalla){
			case 2: //En Funcionamiento Vol. => En Funcionamiento Q.
				if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					intercambioRoles(pantallas, tamagnoPantallas, 7);
					xSemaphoreGive(ArrayPantallasMutex);
				}

				xTaskNotifyGive(ServidorGraficoTaskHandle);
				vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
				xTaskNotifyGive(CompositorGraficoTaskHandle);
				break;

			case 7: //En Funcionamiento Q. => En Funcionamiento Vol.
				if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					intercambioRoles(pantallas, tamagnoPantallas, 2);
					xSemaphoreGive(ArrayPantallasMutex);
				}

				xTaskNotifyGive(ServidorGraficoTaskHandle);
				vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
				xTaskNotifyGive(CompositorGraficoTaskHandle);
				break;
		}

		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}

//Tarea 4. Pulsacion Larga
void PulsacionLargaTask(void *parameter){
	for(;;){
		//Espera infinita la notificacion de la pulsacion larga
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		Serial.println("Pulsacion LARGA");
		sonidoPulsacionLarga();

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos
		indicePantalla = obtenerIndicePantalla();

		//reinicio de valores
		reiniciarValoresHMS();
		reiniciarValoresVolAgua();

		switch (indicePantalla){
			case 1:
				//actualizar valores en el Compositor Grafico
				xTaskNotifyGive(CompositorGraficoTaskHandle);
				break;

			//el mismo funcionamiento para todos los casos
			case 2: case 3: case 4: case 5: case 6: case 7: //en funcionamiento, pausa, finalizado
				detenerSistema();

				//actualizar pantallas y notificar
				if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
					intercambioRoles(pantallas, tamagnoPantallas, 1);
					xSemaphoreGive(ArrayPantallasMutex);
				}
				xTaskNotifyGive(ServidorGraficoTaskHandle);

				//gestor de timers
				xTimerStop(ParpadeoVerdeTimer, 0); //redundante
				xTimerStop(CuentaRegresivaTimer, 0);
				xTimerStop(ParpadeoPausaFinTimer, 0);

				//gestor de tareas
				vTaskSuspend(GestorVolAguaTaskHandle);
				vTaskSuspend(GestorCaudalAguaTaskHandle);
				vTaskSuspend(GestorSonidosTaskHandle);
				vTaskSuspend(IntercambioPantallasTaskHandle);
				//reanudar la tarea de encoder rotatorio (perilla)
				vTaskResume(EncoderRotatorioTaskHandle);
				break;
		}

		//actualizar el estado del sistema a false
		if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			estadoSistema = false;
			xSemaphoreGive(EstadoSistemaMutex);
		}

		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}

//Tarea 5. Servidor Grafico
void ServidorGraficoTask(void *parameter){
	for (;;){
		//Espera infita para actualizar una pantalla
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos y actulizar
		indicePantalla = obtenerIndicePantalla();

		if (xSemaphoreTake(DisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE){
			Serial.printf("Dentro del Servidor Grafico | Pantalla: %d\n", indicePantalla);
			gestorPantallas(indicePantalla); //de lo contrario mostrar la pantalla correspondiente

			xSemaphoreGive(DisplayMutex);
		}
		
		vTaskDelay(2 / portTICK_PERIOD_MS);
	}
}

//Tarea 6. Compositor Grafico | Para las animaciones en los numeros
void CompositorGraficoTask(void *parameter){
	for (;;){
		//Espera infinita para ciertas actualizaciones de pantalla
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
		
		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos y actulizar
		indicePantalla = obtenerIndicePantalla();

		//Variables locales rapidas	
		int horaLocalComp, minLocalComp, segLocalComp;
		if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(5)) == pdTRUE){
			horaLocalComp = hora;	
			minLocalComp = minuto;
			segLocalComp = segundo;
			
			xSemaphoreGive(HoraMinSegMutex);
		}

		//Mutex de proteccion de los caracteres
		if (xSemaphoreTake(DisplayMutex, pdMS_TO_TICKS(50)) == pdTRUE){
			Serial.printf("Dentro del Compositor Grafico | Pantalla: %d\n", indicePantalla);
			dibujarCaracteristicas(horaLocalComp, minLocalComp, segLocalComp, indicePantalla);

			xSemaphoreGive(DisplayMutex);
		}

		vTaskDelay(1 / portTICK_PERIOD_MS);
	}
}

//Tarea 7. Cuenta Regresiva
void CuentaRegresivaTask(void *parameter){
	for (;;){
		//Esta tarea solo puede esperar un poco mas de un segundo 
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		//determinar el estado del sistema
		bool estadoSistema_LocalCR = false;
		if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			estadoSistema_LocalCR = estadoSistema;
			xSemaphoreGive(EstadoSistemaMutex);
		}
			
		//obteniendo la hora, minuto y segundo de los globales
		int hora_LocalCR, minuto_LocalCR, segundo_LocalCR;
		if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			hora_LocalCR = hora;	
			minuto_LocalCR = minuto;
			segundo_LocalCR = segundo;
			
			xSemaphoreGive(HoraMinSegMutex);
		}

		//proceso de decremento del tiempo
		segundo_LocalCR--;
		if (segundo_LocalCR < 0){
			segundo_LocalCR = 59;
			minuto_LocalCR--;
			if (minuto_LocalCR < 0){
				minuto_LocalCR = 59;
				hora_LocalCR--;
			}
		}

		//verificar que el tiempo haya concluido (todos en 0)
		if (segundo_LocalCR == 0 && minuto_LocalCR == 0 && hora_LocalCR == 0 && estadoSistema_LocalCR == true){
			detenerSistema();
			reiniciarValoresHMS();

			//determinar el estado del sistema
			estadoSistema_LocalCR = false;
			if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
				estadoSistema = estadoSistema_LocalCR;
				xSemaphoreGive(EstadoSistemaMutex);
			}

			//cambiar indice a Finalizado y Notificar
			if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
				intercambioRoles(pantallas, tamagnoPantallas, 4);
				xSemaphoreGive(ArrayPantallasMutex);
			}
			xTaskNotifyGive(ServidorGraficoTaskHandle);

			//Guardar el indice de pantalla siguiente | original
			if (xSemaphoreTake(PantallaAntOriginalMutex, pdMS_TO_TICKS(10)) == pdTRUE){
				pantallaAnteriorOriginal = 4;

				xSemaphoreGive(PantallaAntOriginalMutex);
			}

			//gestion de timers
			xTimerStop(CuentaRegresivaTimer, 0);
			xTimerStart(ParpadeoPausaFinTimer, 0);

			//gestion de tareas
			vTaskResume(GestorSonidosTaskHandle);
			vTaskResume(IntercambioPantallasTaskHandle);
			vTaskSuspend(GestorVolAguaTaskHandle);
			vTaskSuspend(GestorCaudalAguaTaskHandle);
		}
		else { //copiar los valores locales a los globales y notificar al Compositor Grafico
			if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(100)) == pdTRUE){
				hora = hora_LocalCR;
				minuto = minuto_LocalCR;
				segundo = segundo_LocalCR;
				
				xSemaphoreGive(HoraMinSegMutex);
			}
			xTaskNotifyGive(CompositorGraficoTaskHandle);
		}

		vTaskDelay(100 / portTICK_PERIOD_MS);
	}
}

//Tarea 8. Supervisor de Nivel de Agua
void SupervisorNivelAguaTask(void *parameter){
	bool nivelAguaLocal = false;
	
	for (;;){
		//debounce para la deteccion del nivel de agua
		if (millis() - lecturaTiempoNivel >= NIVEL_DEBOUNCE){
			nivelAguaLocal = digitalRead(NIVEL) ? true : false;
			//nivelAguaLocal = digitalRead(NIVEL) ? false : true;
			lecturaTiempoNivel = millis();
		}

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos y actulizar
		indicePantalla = obtenerIndicePantalla();

		Serial.printf("El indice de pantalla dentro de Nivel: %d\n", indicePantalla);
		Serial.printf("El nivel leido: %d\n", nivelAguaLocal);
			
		if (nivelAguaLocal == true){
			if (indicePantalla != 8){
				indicePantallaOriginalNivel = indicePantalla;

				//Suspension de Tareas
				vTaskSuspend(EncoderRotatorioTaskHandle);
				vTaskSuspend(EncoderBotonTaskHandle);
				vTaskSuspend(CuentaRegresivaTaskHandle);
				vTaskSuspend(GestorVolAguaTaskHandle);
				vTaskSuspend(GestorCaudalAguaTaskHandle);
				vTaskSuspend(IntercambioPantallasTaskHandle);
				//Reanudar Tareas
				vTaskResume(GestorSonidosTaskHandle);
				
				//Detener el Sistema
				detenerSistema();
				alertaSistema(true); //piloto rojo de alerta de nivel

				switch (indicePantalla){
					case 1: case 2: case 3: case 4: case 5: case 6: case 7: //En cualquier pantalla => Alerta Nivel
						if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
							intercambioRoles(pantallas, tamagnoPantallas, 8);
							xSemaphoreGive(ArrayPantallasMutex);
						}

						xTaskNotifyGive(ServidorGraficoTaskHandle);
						vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
						xTaskNotifyGive(CompositorGraficoTaskHandle); //redundante | para este caso no actualiza nada

						break;
				}
			}
		}
		else {
			if (indicePantalla == 8){
				//Suspender Tareas	
				if (xSemaphoreTake(SonidoMutex, portMAX_DELAY) == pdTRUE){
					vTaskSuspend(GestorSonidosTaskHandle);

					xSemaphoreGive(SonidoMutex);
				}
				//Reanudar Tareas
				vTaskResume(EncoderBotonTaskHandle);
				vTaskResume(CuentaRegresivaTaskHandle);

				alertaSistema(false); //gestion de la alerta

				switch(indicePantallaOriginalNivel){
					case 1: case 4: case 6: //En la Configuracion del Temporizador, Finalizacion, Finalizacion con Intercambios
						//Deterner Sistema | Todo como ha iniciado
						detenerSistema();

						vTaskResume(EncoderRotatorioTaskHandle);
						vTaskResume(EncoderBotonTaskHandle);

						if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
							intercambioRoles(pantallas, tamagnoPantallas, 1);
							xSemaphoreGive(ArrayPantallasMutex);
						}

						xTaskNotifyGive(ServidorGraficoTaskHandle);
						vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
						xTaskNotifyGive(CompositorGraficoTaskHandle);
						break;

					case 2: case 3: case 5: case 7: //En Funcionamiento, En Pausa, En Pausa con Intercambios, En funcionamiento con Caudal
						pausarSistema();
						if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
							intercambioRoles(pantallas, tamagnoPantallas, 3); //pantalla de pausa
							xSemaphoreGive(ArrayPantallasMutex);
						}

						vTaskSuspend(GestorVolAguaTaskHandle);
						vTaskSuspend(GestorCaudalAguaTaskHandle);
						vTaskResume(IntercambioPantallasTaskHandle);
						vTaskResume(GestorSonidosTaskHandle);
						
						//actualizar pantalla
						xTaskNotifyGive(ServidorGraficoTaskHandle);

						//gestion de los timers
						//xTimerStart(ParpadeoVerdeTimer, 0); //0: tiempo de espera para el inicio
						xTimerStart(ParpadeoPausaFinTimer, 0); //temporizdor para el intercambio de pantallas
						xTimerStop(CuentaRegresivaTimer, 0);

						//Guardar el indice de pantalla siguiente | original (para el intercambiador de pantallas)
						if (xSemaphoreTake(PantallaAntOriginalMutex, pdMS_TO_TICKS(10)) == pdTRUE){
							pantallaAnteriorOriginal = 3;

							xSemaphoreGive(PantallaAntOriginalMutex);
						}

						break;
				}
			}
		}

		vTaskDelay(5 / portTICK_PERIOD_MS);
	}	
}

//Tarea 9. Gestion del Volumen de Agua
void GestorVolAguaTask(void *parameter){
	for (;;){
		//esperar el semaforo hasta el infinito
		if (xSemaphoreTake(PulsoCaudalSemaphoreBin, portMAX_DELAY) == pdTRUE){ 
			if (xSemaphoreTake(CantidadPulsosMutex, pdMS_TO_TICKS(100)) == pdTRUE){
				++cantidadPulsos;

				xSemaphoreGive(CantidadPulsosMutex);
			}

			//Para el Volumen de Agua
			if (xSemaphoreTake(VolumenAguaMutex, pdMS_TO_TICKS(50)) == pdTRUE){
				volumenAgua += VOL_PULSOS / PULSOS; //PULSOS [pulsos] = VOL_PULSOS [L]
				
				xSemaphoreGive(VolumenAguaMutex);
			}

			int indicePantalla = -1;
			//verificacion de la pantalla en la que estamos y actulizar
			indicePantalla = obtenerIndicePantalla();

			//notificar al Compositor Grafico
			if (indicePantalla == 2) xTaskNotifyGive(CompositorGraficoTaskHandle);
		}

		vTaskDelay(5 / portTICK_PERIOD_MS);
	}
}

//Tarea 9.1 Gestion del Caudal de Agua
void GestorCaudalAguaTask(void *parameter){
	TickType_t xLastWakeTime = xTaskGetTickCount();
	const TickType_t periodoActualizarCaudal = pdMS_TO_TICKS(TIEMPO_ACTUALIZAR_CAUDAL);

	for (;;){
		int cantidadPulsosLocal = 0;
		if (xSemaphoreTake(CantidadPulsosMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			cantidadPulsosLocal = cantidadPulsos;
			cantidadPulsos = 0;

			xSemaphoreGive(CantidadPulsosMutex);
		}

		//Para el control de Caudal
		float caudalInstantaneoLocal = 0.0f;
		if (cantidadPulsosLocal != 0){
			caudalInstantaneoLocal = ((VOL_PULSOS / PULSOS) * cantidadPulsosLocal) / ((float) (TIEMPO_ACTUALIZAR_CAUDAL / 1000.0f) / 60.0f); //[L/min]
			sumCaudalProm += caudalInstantaneoLocal;
			contadorCaudal++;

			cantidadPulsosLocal = 0; //reiniciando los pulsos
		}
		else caudalInstantaneoLocal = 0.0f;

		//guardar en la variable global de caudal
		if (xSemaphoreTake(CaudalAguaInstanteMutex, pdMS_TO_TICKS(50)) == pdTRUE){
			caudalAguaInstante = caudalInstantaneoLocal;

			xSemaphoreGive(CaudalAguaInstanteMutex);
		}

		//notificar al Compositor Grafico
		xTaskNotifyGive(CompositorGraficoTaskHandle);

		vTaskDelayUntil(&xLastWakeTime, periodoActualizarCaudal); //delay con compesacion por tiempo perdido en procesamiento
	}
}

//Tarea 10. Gestor de Sonidos de Alertas
void GestorSonidosTask(void *parameter){
	for (;;){
		//verificar en que pantalla nos encotramos
		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos y actulizar
		indicePantalla = obtenerIndicePantalla();

		if (xSemaphoreTake(SonidoMutex, portMAX_DELAY) == pdTRUE){
			switch (indicePantalla){
				case 4: case 6: sonidoFinalizacion(); break;
				case 3: case 5: sonidoPausa(); break; 
				case 8: sonidoNivelBajo(); break;
			}

			xSemaphoreGive(SonidoMutex);
		}

		vTaskDelay(5 / portTICK_PERIOD_MS);
	}
}

//Tarea 11. Intercambio de Pantallas en Pausa y Finalizacion
void IntercambioPantallasTask(void *parameter){
	for (;;){
		//Espera hasta el infito
		ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

		//obteniendo el estado del sistema de la variable global
		bool estadoSistema_LocalICP = false;
		if (xSemaphoreTake(EstadoSistemaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
			estadoSistema_LocalICP = estadoSistema;

			xSemaphoreGive(EstadoSistemaMutex);
		}

		//saber cual ha sido la pantalla anterior
		int pantallaOriginal_Local = -1;
		if (xSemaphoreTake(PantallaAntOriginalMutex, pdMS_TO_TICKS(10)) == pdTRUE){
			pantallaOriginal_Local = pantallaAnteriorOriginal;

			xSemaphoreGive(PantallaAntOriginalMutex);
		}

		int indicePantalla = -1;
		//verificacion de la pantalla en la que estamos y actulizar
		indicePantalla = obtenerIndicePantalla();
		switch (pantallaOriginal_Local){
			case 3:
				if (indicePantalla != 5){
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, 5);
						xSemaphoreGive(ArrayPantallasMutex);
					}

					xTaskNotifyGive(ServidorGraficoTaskHandle);
					vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
					xTaskNotifyGive(CompositorGraficoTaskHandle);
				}
				else {
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, pantallaOriginal_Local);

						xSemaphoreGive(ArrayPantallasMutex);
					}
					xTaskNotifyGive(ServidorGraficoTaskHandle);

					Serial.printf("Pantalla Anterior Original: %d\n", pantallaOriginal_Local);
				}
				break;

			case 4:
				if (indicePantalla != 6){
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, 6);
						xSemaphoreGive(ArrayPantallasMutex);
					}
					
					xTaskNotifyGive(ServidorGraficoTaskHandle);
					vTaskDelay(5 / portTICK_PERIOD_MS); //esperando un tiempo para que se establesca la pantalla
					xTaskNotifyGive(CompositorGraficoTaskHandle);
				}
				else {
					if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
						intercambioRoles(pantallas, tamagnoPantallas, pantallaOriginal_Local);

						xSemaphoreGive(ArrayPantallasMutex);
					}
					xTaskNotifyGive(ServidorGraficoTaskHandle);

					Serial.printf("Pantalla Anterior Original: %d\n", pantallaOriginal_Local);
				}
				break;
		}

		vTaskDelay(5 / portTICK_PERIOD_MS);
	}
}

//=========================================================
void setup() {
	Serial.begin(115200);

	//********************************************************
	//Configuracion de los pines de entrada
	pinMode(CLK, INPUT_PULLUP);
	pinMode(DT, INPUT_PULLUP);
	pinMode(SW, INPUT_PULLUP);
	//Configuracion de los pines de salida
	pinMode(VERDE, OUTPUT);
	pinMode(ROJO, OUTPUT);
	pinMode(BOMBA, OUTPUT);
	pinMode(VENT, OUTPUT);
	pinMode(BUZZER, OUTPUT);
	pinMode(NIVEL, INPUT_PULLUP); //pin de entrada para el nivel de agua
	pinMode(CAUDAL, INPUT); //pin de entrada para el caudalimetro

	//********************************************************
	//Para el encoder rotatorio | Asignar el ultimo estado de CLK
	estadoUltimoCLK = digitalRead(CLK) ? true : false;

	//********************************************************
	//Para el LCD
	Wire.begin(SDA, SCL);
	lcd.init();
	lcd.backlight();
	lcd.createChar(0, simboloPlay);
	lcd.createChar(1, simboloPausa);
	lcd.createChar(2, simboloParo);
	lcd.createChar(3, simboloFlecha);
	lcd.createChar(4, simboloInterrogacionInicial);

	//********************************************************
	//Creacion de los Semaforos
	EstadoSistemaMutex = xSemaphoreCreateMutex();
	ArrayPantallasMutex = xSemaphoreCreateMutex();
	HoraMinSegMutex = xSemaphoreCreateMutex();
	HoraMinSegInicialMutex = xSemaphoreCreateMutex();
	VolumenAguaMutex = xSemaphoreCreateMutex();
	CaudalAguaInstanteMutex = xSemaphoreCreateMutex();
	CantidadPulsosMutex = xSemaphoreCreateMutex();
	PantallaAntOriginalMutex = xSemaphoreCreateMutex();
	DisplayMutex = xSemaphoreCreateMutex();
	SonidoMutex = xSemaphoreCreateMutex();
	PulsoCaudalSemaphoreBin = xSemaphoreCreateBinary();

	//********************************************************
	//Configurando la interrupcion para el caudalimetro | iniciar despues de crear los semaforos
	attachInterrupt(digitalPinToInterrupt(CAUDAL), notificarCaudal,	RISING);

	//Creacion de Timers
	//Timer 1. Parpadeo de Piloto Verde en estado de Pausa
	ParpadeoVerdeTimer = xTimerCreate(
		"ParpadeoVerdeTimer", //nombre del timer
		pdMS_TO_TICKS(1000), //periodo de parpadeo
		pdTRUE, //autoreload, timer periodico
		NULL, //sin ID
		parpadeoVerde //funcion callback
	);

	//Timer 2. Cuenta regresiva 
	CuentaRegresivaTimer = xTimerCreate(
		"CuentaRegresivaTimer", //nombre del timer
		pdMS_TO_TICKS(1000), //periodo de parpadeo
		pdTRUE, //autoreload, timer periodico
		NULL, //sin ID
		cuentaRegresiva //funcion callback
	);
	
	//Timer 3. Parpadeo de Pantalla Pausa-Fin
	ParpadeoPausaFinTimer = xTimerCreate(
		"ParpadeoPausaFinTimer", //nombre del timer
		pdMS_TO_TICKS(2000), //periodo de parpadeo
		pdTRUE, //autoreload, timer periodico
		NULL, //sin ID
		parpadeoPausaFin//funcion callback
	);


	//Creacion de las Tareas
	//Tarea 1. Encoder Rotatorio
	xTaskCreatePinnedToCore(
		EncoderRotatorioTask, 
		"EncoderRotatorioTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&EncoderRotatorioTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 2. Boton del Encoder Rotatorio
	xTaskCreatePinnedToCore(
		EncoderBotonTask, 
		"EncoderBotonTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&EncoderBotonTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 3. Pulsacion Corta
	xTaskCreatePinnedToCore(
		PulsacionCortaTask, 
		"PulsacionCortaTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&PulsacionCortaTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 3.1 Pulsacion Doble
	xTaskCreatePinnedToCore(
		PulsacionDobleTask, 
		"PulsacionDobleTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&PulsacionDobleTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 4. Pulsacion Larga
	xTaskCreatePinnedToCore(
		PulsacionLargaTask, 
		"PulsacionLargaTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&PulsacionLargaTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 5. Servidor Grafico
	xTaskCreatePinnedToCore(
		ServidorGraficoTask, 
		"ServidorGraficoTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&ServidorGraficoTaskHandle,
		0 //nucleo en el procesador
	);

	//Tarea 6. Compositor Grafico | Para las animaciones en los numeros
	xTaskCreatePinnedToCore(
		CompositorGraficoTask, 
		"CompositorGraficoTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&CompositorGraficoTaskHandle,
		0 //nucleo en el procesador
	);

	//Tarea 7. Cuenta Regresiva
	xTaskCreatePinnedToCore(
		CuentaRegresivaTask, 
		"CuentaRegresivaTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&CuentaRegresivaTaskHandle,
		0 //nucleo en el procesador
	);

	//Tarea 8. Supervisor de Nivel de Agua
	xTaskCreatePinnedToCore(
		SupervisorNivelAguaTask, 
		"SupervisorNivelAguaTask",
		4096, //memoria en el stack
		NULL,
		1, //prioridad
		&SupervisorNivelAguaTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 9. Gestion del Volumen de Agua
	xTaskCreatePinnedToCore(
		GestorVolAguaTask, 
		"GestorVolAguaTask",
		2048, //memoria en el stack
		NULL,
		1, //prioridad
		&GestorVolAguaTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 9.1 Gestion del Caudal de Agua
	xTaskCreatePinnedToCore(
		GestorCaudalAguaTask, 
		"GestorCaudalAguaTask",
		2048, //memoria en el stack
		NULL,
		1, //prioridad
		&GestorCaudalAguaTaskHandle,
		1 //nucleo en el procesador
	);

	//Tarea 10. Gestor de Sonidos de Alertas
	xTaskCreatePinnedToCore(
		GestorSonidosTask, 
		"GestorSonidosTask",
		2048, //memoria en el stack
		NULL,
		1, //prioridad
		&GestorSonidosTaskHandle,
		0 //nucleo en el procesador
	);

	//Tarea 11. Intercambio de Pantallas en Pausa y Finalizacion
	xTaskCreatePinnedToCore(
		IntercambioPantallasTask, 
		"IntercambioPantallasTask",
		2048, //memoria en el stack
		NULL,
		1, //prioridad
		&IntercambioPantallasTaskHandle,
		0 //nucleo en el procesador
	);

	lecturaTiempoNivel = millis();
	//********************************************************
	//Iniciando Sistema
	vTaskSuspend(SupervisorNivelAguaTaskHandle);
	vTaskSuspend(GestorVolAguaTaskHandle);
	vTaskSuspend(GestorCaudalAguaTaskHandle);
	vTaskSuspend(GestorSonidosTaskHandle);
	vTaskSuspend(IntercambioPantallasTaskHandle);
	gestorPantallas(0);
	vTaskDelay(800 / portTICK_PERIOD_MS);
	gestorPantallas(1);
	vTaskDelay(200 / portTICK_PERIOD_MS);
	vTaskResume(SupervisorNivelAguaTaskHandle);

	Serial.println("Sistema iniciado");
}

//=========================================================
void loop() {
}

//=========================================================
//FUNCIONES
//********************************************************
//Obtencion del Indice de Pantalla
int obtenerIndicePantalla(){
	int indicePantalla = -1;
	if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
		for (int i = 0; i < tamagnoPantallas; i++){
			if (pantallas[i] == true){
				indicePantalla = i;
				break;
			}
		}

		xSemaphoreGive(ArrayPantallasMutex);
	}

	return indicePantalla;
}

//Para la Gestion de Pantallas
void gestorPantallas(int pantalla){
	lcd.clear();

	if (xSemaphoreTake(ArrayPantallasMutex, pdMS_TO_TICKS(10)) == pdTRUE){
		intercambioRoles(pantallas, tamagnoPantallas, pantalla);
		xSemaphoreGive(ArrayPantallasMutex);
	}

	switch (pantalla){
		case 0: //Bienvenida
			lcd.setCursor(3, 0);
			lcd.print("BIENVENIDO");
			lcd.setCursor(4, 1);
			lcd.print("IEM-UMSA");
			break;

		case 1: //Ajustar Temporizador
			lcd.setCursor(1, 0);
			lcd.print("AJUSTAR TIEMPO");
			lcd.setCursor(1, 1);
			lcd.write((uint8_t) 3); //simbolo Flecha
			lcd.setCursor(3, 1);
			lcd.print("00:00:00");
			break;

		case 2: //En Funcionaiento con Vol.
			lcd.setCursor(0, 0);
			lcd.print("VOL.:");
			lcd.setCursor(12, 0);
			lcd.print("L");
			lcd.setCursor(15, 0);
			lcd.write((uint8_t) 0); //simbolo Play
			lcd.setCursor(0, 1);
			lcd.print("TIEMPO: 00:00:00");
			break;

		case 3: //En Pausa
			lcd.setCursor(4, 0);
			lcd.print("EN PAUSA");	
			lcd.setCursor(1, 1);
			lcd.print("Click");
			lcd.setCursor(6, 1);
			lcd.write((uint8_t) 3); //simbolo Flecha
			lcd.setCursor(7, 1);
			lcd.print("Reanudar");
			break;

		case 4: //Finalizado
			lcd.setCursor(3, 0);
			lcd.print("FINALIZADO");	
			lcd.setCursor(2, 1);
			lcd.print("Click");
			lcd.setCursor(7, 1);
			lcd.write((uint8_t) 3); //simbolo Flecha
			lcd.setCursor(8, 1);
			lcd.print("Inicio");
			break;

		case 5: //En Pausa | Pantalla en Intercambios con Vol.
			lcd.setCursor(0, 0);
			lcd.print("VOL.:");
			lcd.setCursor(12, 0);
			lcd.print("L");
			lcd.setCursor(15, 0);
			lcd.write((uint8_t) 1); //simbolo Pausa
			lcd.setCursor(0, 1);
			lcd.print("TIEMPO: 00:00:00");
			break;

		case 6: //Finalizado | Pantalla en Intercambios con Vol.
			lcd.setCursor(0, 0);
			lcd.print("VOL.:");
			lcd.setCursor(12, 0);
			lcd.print("L");
			lcd.setCursor(15, 0);
			lcd.write((uint8_t) 2); //simbolo Paro
			lcd.setCursor(0, 1);
			lcd.print("Qprom:");
			lcd.setCursor(12, 1);
			lcd.print("L/mi");
			break;

		case 7: //En Funcionamiento con Caudal
			lcd.setCursor(0, 0);
			lcd.print("Q:");
			lcd.setCursor(9, 0);
			lcd.print("L/min");
			lcd.setCursor(15, 0);
			lcd.write((uint8_t) 0); //simbolo Play
			lcd.setCursor(0, 1);
			lcd.print("TIEMPO: 00:00:00");
			break;

		case 8: //Nivel de Agua Bajo
			lcd.setCursor(3, 0);
			lcd.write((uint8_t) 4); //simbolo de interrogacion inicial
			lcd.setCursor(4, 0);
			lcd.print("NIVEL DE");
			lcd.setCursor(3, 1);
			lcd.print("AGUA BAJO!");
			break;
	}
}

//Dibujar ciertas caracteristicas en pantalla
void dibujarCaracteristicas(int hora, int minuto, int segundo, int pantalla){
	switch (pantalla){
		case 1: //En la pantalla de ajuste del Temporizador
			dibujarCaracteristicasTiempo(1);
			break;

		case 2: case 5: { //en las pantallas de Funcionamiento Vol., Pausa de Intercambios Vol. y Final Vol.
			float volumenAguaLocal;
			//obteniendo el volumen de agua de la variable global
			if (xSemaphoreTake(VolumenAguaMutex, pdMS_TO_TICKS(50)) == pdTRUE){
				volumenAguaLocal = volumenAgua;
				
				xSemaphoreGive(VolumenAguaMutex);
			}
			dibujarCaracteristicasTiempo(2);

			//dibular el volumen de agua
			lcd.setCursor(6, 0);
			lcd.print(volumenAguaLocal);
			break;	
		} //final de llave case 2:

		case 6: {  //Final
			float volumenAguaLocal;
			//obteniendo el volumen de agua de la variable global
			if (xSemaphoreTake(VolumenAguaMutex, pdMS_TO_TICKS(50)) == pdTRUE){
				volumenAguaLocal = volumenAgua;
				
				xSemaphoreGive(VolumenAguaMutex);
			}

			//dibular el volumen de agua
			lcd.setCursor(6, 0);
			lcd.print(volumenAguaLocal);
			//dibular el caudal promedio
			lcd.setCursor(7, 1);
			lcd.print(((float) (sumCaudalProm / contadorCaudal)), 1);
			break;
		}

		case 7: {//en las pantallas de Funcionamiento con Q.
			float caudalAguaLocal;

			//obteniendo el caudal de agua de la variable global
			if (xSemaphoreTake(CaudalAguaInstanteMutex, pdMS_TO_TICKS(50)) == pdTRUE){
				caudalAguaLocal = caudalAguaInstante;

				xSemaphoreGive(CaudalAguaInstanteMutex);
			}
			dibujarCaracteristicasTiempo(2);

			//dibular el caudal de agua
			lcd.setCursor(3, 0);
			lcd.print(caudalAguaLocal);
			break;
		}
	}
}

//Dibujar caracteristicas de tiempo en pantalla
void dibujarCaracteristicasTiempo(int tipo){
	switch (tipo){
		case 1: //Tiempo en el Ajuste de Temporizador
			//para mostrar la hora
			if (0 <= hora && hora <= 9){
				lcd.setCursor(3, 1);
				lcd.print("0");
				lcd.setCursor(4, 1);	
				lcd.print(hora);
			}
			else {
				lcd.setCursor(3, 1);
				lcd.print(hora);
			}

			//para mostrar los minutos
			if (0 <= minuto && minuto <= 9){
				lcd.setCursor(6, 1);
				lcd.print("0");
				lcd.setCursor(7, 1);	
				lcd.print(minuto);
			}
			else {
				lcd.setCursor(6, 1);
				lcd.print(minuto);
			}
			break;

		case 2: //Tiempo en Cuentas Regresivas
			//para mostrar la hora
			if (0 <= hora && hora <= 9){
				lcd.setCursor(8, 1);
				lcd.print("0");
				lcd.setCursor(9, 1);	
				lcd.print(hora);
			}
			else {
				lcd.setCursor(8, 1);
				lcd.print(hora);
			}

			//para mostrar los minutos
			if (0 <= minuto && minuto <= 9){
				lcd.setCursor(11, 1);
				lcd.print("0");
				lcd.setCursor(12, 1);	
				lcd.print(minuto);
			}
			else {
				lcd.setCursor(11, 1);
				lcd.print(minuto);
			}

			//para mostrar los segundos
			if (0 <= segundo && segundo <= 9){
				lcd.setCursor(14, 1);
				lcd.print("0");
				lcd.setCursor(15, 1);	
				lcd.print(segundo);
			}
			else {
				lcd.setCursor(14, 1);
				lcd.print(segundo);
			}
			break;
	}
}

//********************************************************
//Funcion para cambio de roles
void intercambioRoles(volatile bool arreglo[], volatile int tamagno_arreglo, int indice){
	if (0 <= indice && indice < tamagno_arreglo){
		for (int i = 0; i < tamagno_arreglo; i++){
			arreglo[i] = false;
		}
		arreglo[indice] = true;
	}
}

//********************************************************
//Funcion de Reinicio de Valores de Hora, Minuto y Segundo
void reiniciarValoresHMS(){
	if (xSemaphoreTake(HoraMinSegMutex, pdMS_TO_TICKS(100)) == pdTRUE){
		hora = minuto = segundo = 0;

		xSemaphoreGive(HoraMinSegMutex);
	}
}

//Funcion para el reincio de valores para el Volumen de Agua
void reiniciarValoresVolAgua(){
	if (xSemaphoreTake(VolumenAguaMutex, pdMS_TO_TICKS(100)) == pdTRUE){
		volumenAgua = 0.0;			

		xSemaphoreGive(VolumenAguaMutex);
	}
	sumCaudalProm = 0.0f;
}

//********************************************************
//Acciones del Sistema
void iniciarSistema(){
	digitalWrite(VERDE, HIGH);
	digitalWrite(BOMBA, HIGH);
	digitalWrite(VENT, HIGH);
	digitalWrite(ROJO, LOW);
}

void pausarSistema(){
	digitalWrite(BOMBA, LOW);
	digitalWrite(VENT, LOW);
	digitalWrite(VERDE, HIGH);
	digitalWrite(ROJO, LOW);
}

void detenerSistema(){
	digitalWrite(VERDE, LOW);
	digitalWrite(BOMBA, LOW);
	digitalWrite(VENT, LOW);
}

void alertaSistema(bool opcion){
	if (opcion) digitalWrite(ROJO, HIGH);
	else digitalWrite(ROJO, LOW);
}

//********************************************************
//Funciones Callback para los Timers
//Parpadeo de Piloto Verde en estado de Pausa del Sistema
void parpadeoVerde(TimerHandle_t xTimer){
	static bool estado = true; //ya que el piloto se encontraba encendido 
	estado = !estado;
	digitalWrite(VERDE, estado);
}

//Parpadeo de Pantalla en Estados de Pausa y Fin de funcionamiento
void parpadeoPausaFin(TimerHandle_t xTimer){
	xTaskNotifyGive(IntercambioPantallasTaskHandle);
}

//Cuenta regresiva en el funcionamiento del sistema
void cuentaRegresiva(TimerHandle_t xTimer){
	xTaskNotifyGive(CuentaRegresivaTaskHandle);
}

//********************************************************
//Funciones Callback para las Interrupciones
void IRAM_ATTR notificarCaudal(){
	BaseType_t xHigherPriorityTaskWoken = pdFALSE;

	//enviando una segnal mediante el semaforo
	xSemaphoreGiveFromISR(PulsoCaudalSemaphoreBin, &xHigherPriorityTaskWoken);

	//solicitando cambio de contexto (opcional)
	if (xHigherPriorityTaskWoken){
		portYIELD_FROM_ISR();
	}
}

//********************************************************
//Tonos para el Buzzer
//Pulsacion Corta
void sonidoPulsacionCorta() {
    tone(BUZZER, 800);
	vTaskDelay(30 / portTICK_PERIOD_MS);
    noTone(BUZZER);
}

//Confirmacion
void sonidoConfirmar() {
    tone(BUZZER, 1000);
	vTaskDelay(100 / portTICK_PERIOD_MS);
    tone(BUZZER, 1500);
	vTaskDelay(150 / portTICK_PERIOD_MS);
    noTone(BUZZER);
}

//Pulsacion Larga
void sonidoPulsacionLarga() {
    tone(BUZZER, 600);
	vTaskDelay(150 / portTICK_PERIOD_MS);
    tone(BUZZER, 400);
	vTaskDelay(200 / portTICK_PERIOD_MS);
    noTone(BUZZER);
}

//Alerta de Finalizacion
void sonidoFinalizacion() {
    for (int i = 0; i < 3; i++) {
        tone(BUZZER, 2500);
		vTaskDelay(200 / portTICK_PERIOD_MS);
        noTone(BUZZER);
		vTaskDelay(200 / portTICK_PERIOD_MS);
    }
}

//Alerta de Pausa
void sonidoPausa() {
    for (int i = 0; i < 2; i++) {
        tone(BUZZER, 900);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        tone(BUZZER, 600);
        vTaskDelay(100 / portTICK_PERIOD_MS);
        noTone(BUZZER);
        vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    noTone(BUZZER);
}

//Nivel de Agua Bajo
void sonidoNivelBajo() {
    for (int i = 0; i < 5; i++) {
        tone(BUZZER, 1200);
		vTaskDelay(200 / portTICK_PERIOD_MS);
        tone(BUZZER, 800);
		vTaskDelay(200 / portTICK_PERIOD_MS);
    }
    noTone(BUZZER);
}

