#include <Arduino.h>

const int PIN_MQ2       = 34;
const int PIN_LED_VERDE = 16;
const int PIN_LED_VERM  = 17;
const int PIN_BUZZER    = 25;
const int PIN_BOTAO     = 32;

const bool USAR_BUZZER = false;
const bool USAR_BOTAO  = false;

// ---------------- Limiares (CALIBRAR) ----------------
const int TH_ATENCAO_UP   = 1500;
const int TH_ATENCAO_DOWN = 1200;   // histerese
const int TH_ALARME_UP    = 2400;

const unsigned long TEMPO_AQUECIMENTO_MS = 60000;
const unsigned long INTERVALO_LEITURA_MS = 200;
const unsigned long INTERVALO_PRINT_MS   = 2000;
const int AMOSTRAS_MEDIA = 10;
const int N_CONFIRMA     = 5;     // leituras seguidas para mudar de estado
const int FALHA_MIN = 20, FALHA_MAX = 4080, N_FALHA = 25;

// ---------------- Estado ----------------
enum Estado { AQUECENDO, NORMAL, ATENCAO, ALARME, FALHA };
Estado estado = AQUECENDO;

int  buf[AMOSTRAS_MEDIA];
int  idx = 0, qtd = 0;
long soma = 0;
int  contConfirma = 0, contFalha = 0;
unsigned long tLeitura = 0, tPrint = 0;

const char* nomeEstado(Estado e) {
  static const char* nomes[] = {"AQUECENDO", "NORMAL", "ATENCAO", "ALARME", "FALHA"};
  return nomes[e];
}

const char* aviso(Estado e) {
  switch (e) {
    case AQUECENDO: return "Sensor aquecendo. Aguarde.";
    case NORMAL:    return "Ar normal.";
    case ATENCAO:   return "Gas detectado em nivel baixo. Verifique o ambiente.";
    case ALARME:    return "NIVEL PERIGOSO DE GAS! Ventile o local e afaste fontes de ignicao.";
    case FALHA:     return "Leitura invalida. Verifique fiacao e alimentacao do MQ-2.";
  }
  return "";
}

// ---------------- Leitura com média móvel ----------------
int lerSensor() {
  int v = analogRead(PIN_MQ2);
  soma += v - buf[idx];
  buf[idx] = v;
  idx = (idx + 1) % AMOSTRAS_MEDIA;
  if (qtd < AMOSTRAS_MEDIA) qtd++;
  return soma / qtd;
}

bool botaoPressionado() {
  return USAR_BOTAO && digitalRead(PIN_BOTAO) == LOW;
}

// ---------------- Mudança de estado (com aviso) ----------------
void mudarEstado(Estado novo, int leitura) {
  Serial.printf("\n>>> %s -> %s (leitura=%d)\n    %s\n\n",
                nomeEstado(estado), nomeEstado(novo), leitura, aviso(novo));
  estado = novo;
  contConfirma = 0;
}

// Estado que a leitura atual está pedindo
Estado estadoDesejado(int leitura) {
  switch (estado) {
    case NORMAL:
      if (leitura > TH_ALARME_UP)  return ALARME;
      if (leitura > TH_ATENCAO_UP) return ATENCAO;
      return NORMAL;
    case ATENCAO:
      if (leitura > TH_ALARME_UP)         return ALARME;
      if (leitura < TH_ATENCAO_DOWN)      return NORMAL;
      return ATENCAO;
    case ALARME:
      // Com botão: só sai com ar limpo + botão. Sem botão: sai sozinho.
      if (leitura < TH_ATENCAO_DOWN && (!USAR_BOTAO || botaoPressionado())) return NORMAL;
      return ALARME;
    default:
      return estado;
  }
}

void atualizarEstado(int leitura) {
  // Leitura impossível por muito tempo = FALHA
  if (leitura <= FALHA_MIN || leitura >= FALHA_MAX) {
    if (contFalha < N_FALHA) contFalha++;
    if (contFalha >= N_FALHA && estado != FALHA && estado != ALARME) mudarEstado(FALHA, leitura);
    return;
  }
  contFalha = 0;

  if (estado == FALHA) {
    mudarEstado(millis() < TEMPO_AQUECIMENTO_MS ? AQUECENDO : NORMAL, leitura);
    return;
  }
  if (estado == AQUECENDO) {
    if (millis() >= TEMPO_AQUECIMENTO_MS) mudarEstado(NORMAL, leitura);
    return;
  }

  // Só muda depois de N_CONFIRMA leituras seguidas pedindo a mesma mudança
  Estado d = estadoDesejado(leitura);
  contConfirma = (d != estado) ? contConfirma + 1 : 0;
  if (contConfirma >= N_CONFIRMA) mudarEstado(d, leitura);
}

// ---------------- LEDs e buzzer ----------------
void atualizarSaidas() {
  bool piscaRapido = (millis() / 150) % 2;
  bool piscaLento  = (millis() / 500) % 2;
  bool verde = false, vermelho = false, buzzer = false;

  switch (estado) {
    case AQUECENDO: verde = piscaRapido;                        break;
    case NORMAL:    verde = true;                               break;
    case ATENCAO:   vermelho = piscaLento;                      break;
    case ALARME:    vermelho = true; buzzer = true;             break;
    case FALHA:     verde = piscaLento; vermelho = !piscaLento; break;
  }

  digitalWrite(PIN_LED_VERDE, verde);
  digitalWrite(PIN_LED_VERM, vermelho);
  if (USAR_BUZZER) digitalWrite(PIN_BUZZER, buzzer);
}

// ---------------- Linha de status periódica ----------------
void imprimirStatus(int leitura) {
  if (estado == AQUECENDO) {
    Serial.printf("[%s] leitura=%4d  faltam %lu s\n", nomeEstado(estado), leitura,
                  (TEMPO_AQUECIMENTO_MS - millis()) / 1000);
  } else {
    Serial.printf("[%s] leitura=%4d\n", nomeEstado(estado), leitura);
  }
}

// ---------------- Setup / Loop ----------------
void setup() {
  Serial.begin(115200);
  pinMode(PIN_LED_VERDE, OUTPUT);
  pinMode(PIN_LED_VERM, OUTPUT);
  if (USAR_BUZZER) { pinMode(PIN_BUZZER, OUTPUT); digitalWrite(PIN_BUZZER, LOW); }
  if (USAR_BOTAO)  pinMode(PIN_BOTAO, INPUT_PULLUP);
  analogSetPinAttenuation(PIN_MQ2, ADC_11db);
  Serial.println("\n[GuardGas] Protótipo iniciado.");
  Serial.println(aviso(AQUECENDO));
}

void loop() {
  unsigned long agora = millis();
  if (agora - tLeitura >= INTERVALO_LEITURA_MS) {
    tLeitura = agora;
    int leitura = lerSensor();
    atualizarEstado(leitura);
    if (agora - tPrint >= INTERVALO_PRINT_MS) {
      tPrint = agora;
      imprimirStatus(leitura);
    }
  }
  atualizarSaidas();
}
