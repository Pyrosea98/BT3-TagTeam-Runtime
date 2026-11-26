/*
 * Sangre: quien sangra, de que color, y con que golpes (reglas de Juan, 2026-11-19).
 *
 * El personaje se clasifica por el NOMBRE del roster (Port_CharaName), no por id: el roster del mod
 * ya tiene cada personaje separado y el nombre se lee del propio juego, asi que la tabla no depende
 * de numeros que cambien cuando se agrega un personaje.
 *
 * Colores:
 *   rojo            por defecto (Saiyans, humanos, Recoome, Uub, posesiones de Baby)
 *   violeta         piel verde: Piccolo, Cell, Saibaman, Tambourine, Drum, Cymbal, Slug, Garlic Jr.
 *                   y los dragones de GT (Nouva, Eis, Syn, Omega)
 *   violeta claro   Zarbon, Dabura, Ginyu, Guldo, Cooler
 *   (Freezer y todas sus variantes, Mecha Freezer incluido, sangran ROJO: Juan 2026-11-19)
 *   rosa            Jeice
 *   azul oscuro     Burter
 *   violeta oscuro  Janemba (propuesta)
 *   sin sangre      familia Buu, Baby parasito (Baby, 1st stage), robots/androides salvo 17 y 18,
 *                   gelatinas (Bio-Broly: cuerpo de bio-liquido), maquinas (Dr. Gero/Android 20 cuerpo
 *                   androide, Dr. Wheelo cerebro en robot, Hatchiyack), y los munecos (Tamagami)
 *
 * Un "sin sangre" no sangra ni acumula desgaste: solo muestra su modelo de traje danado.
 *
 * Los nombres salen de la ISO del jugador, que puede estar en ingles o en espanol: cada regla busca las dos
 * formas ("Android"/"Androide", "Freezer"/"Frieza", "Buu"/"Boo", "Piccolo"/"Piccoro", "Nouva"/"Nuova").
 * "Android" tambien encuentra "Androide" (es un prefijo).
 */
#ifndef MOD_BLOOD_H
#define MOD_BLOOD_H
#include <stddef.h>

extern const char *Port_CharaName(int chara); /* plat_fighters.c: the roster name of a character */

static int blood_lower(int c) {
    return c >= 'A' && c <= 'Z' ? c + 32 : c;
}

/* 1 si `name` contiene `key` sin distinguir mayusculas. */
static int blood_has(const char *name, const char *key) {
    size_t i, n = 0;
    while(key[n])n++;
    if (name == NULL || *name == '\0' || n == 0) {
        return 0;
    }
    for (i = 0; name[i] != '\0'; i++) {
        size_t k = 0;
        while (k < n && name[i + k] != '\0' && blood_lower((unsigned char)name[i + k]) == blood_lower((unsigned char)key[k])) {
            k++;
        }
        if (k == n) {
            return 1;
        }
    }
    return 0;
}

/* 1 = este personaje no sangra ni acumula dano. */
static int Blood_None(int chara) {
    const char *n = Port_CharaName(chara);
    if (n == NULL || *n == '\0') {
        return 0; /* sin nombre todavia: se comporta como normal */
    }
    /* Posesiones: el cuerpo es Saiyan/humano y ese cuerpo sangra. */
    if (blood_has(n, "Baby Goku") || blood_has(n, "Baby Vegeta") || blood_has(n, "Baby-Baby")) {
        return 0;
    }
    if (blood_has(n, "Buu") || blood_has(n, "Boo") || blood_has(n, "Majin Kuu") || blood_has(n, "Majin Duu")) {
        return 1; /* toda la familia: Kid, Super, Fat, absorciones, Majin Kuu/Duu */
    }
    if (blood_has(n, "Baby")) {
        return 1; /* parasito y 1st stage */
    }
    /* Super 17 es la fusion con el 17 infernal: maquina, no sangra. Va ANTES de la regla del 17, porque un
       nombre como "Super Androide 17" tambien contiene "Androide 17". */
    if (blood_has(n, "Super 17") || blood_has(n, "Super #17") || blood_has(n, "Super Android")) {
        return 1;
    }
    /* Androides: solo 17 y 18 sangran (no son totalmente roboticos). Cubre "Android 17", "Android #17" y
       "Androide 17 (GT - Tierra)". */
    if (blood_has(n, "Android") && (blood_has(n, "17") || blood_has(n, "18"))) {
        return 0;
    }
    if (blood_has(n, "Android") || blood_has(n, "Cell Jr")) {
        return 1;
    }
    /* Gelatinas: Bio-Broly es bio-liquido de cultivo (como la familia Buu). */
    if (blood_has(n, "Bio-Broly") || blood_has(n, "Bio Broly")) {
        return 1;
    }
    /* Maquinas: Dr. Gero (su cuerpo es el Androide 20), Dr. Wheelo (cerebro en robot), Hatchiyack. */
    if (blood_has(n, "Gero") || blood_has(n, "Wheelo") || blood_has(n, "Willow") || blood_has(n, "Hatchiyack") ||
        blood_has(n, "Hatchiyak")) {
        return 1;
    }
    if (blood_has(n, "Metal") || blood_has(n, "robot") || blood_has(n, "Lychee") || blood_has(n, "Rilld") ||
        blood_has(n, "Rildo")) {
        return 1;
    }
    if (blood_has(n, "Tamagami") || blood_has(n, "Luud")) {
        return 1; /* munecos/maquinas */
    }
    return 0;
}

/* Color RGB (0..255) de la sangre de `chara`. */
static void Blood_Color(int chara, int *r, int *g, int *b) {
    const char *n = Port_CharaName(chara);
    int R = 172, G = 22, B = 28; /* rojo por defecto */
    if (n != NULL) {
        if (blood_has(n, "Zarbon") || blood_has(n, "Dabura") || blood_has(n, "Ginyu") || blood_has(n, "Guldo") ||
            blood_has(n, "Cooler")) {
            R = 198; G = 126; B = 226; /* violeta claro */
        } else if (blood_has(n, "Jeice")) {
            R = 255; G = 92; B = 176; /* rosa / fucsia */
        } else if (blood_has(n, "Burter")) {
            R = 62; G = 96; B = 204; /* azul oscuro */
        } else if (blood_has(n, "Janemba")) {
            R = 124; G = 44; B = 158; /* violeta oscuro (propuesta) */
        } else if (blood_has(n, "Piccolo") || blood_has(n, "Piccoro") || blood_has(n, "Cell") || blood_has(n, "Saibam") ||
                   blood_has(n, "Tambourine") || blood_has(n, "Drum") || blood_has(n, "Cymbal") ||
                   blood_has(n, "Slug") || blood_has(n, "Garlic") ||
                   blood_has(n, "Nouva") || blood_has(n, "Nuova") || blood_has(n, "Eis") || blood_has(n, "Syn") || blood_has(n, "Omega")) {
            R = 176; G = 64; B = 202; /* violeta (piel verde / dragones) */
        }
    }
    if (r) { *r = R; }
    if (g) { *g = G; }
    if (b) { *b = B; }
}

#endif
