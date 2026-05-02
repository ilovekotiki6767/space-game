#ifndef GAMING_GAME_STRING_H
#define GAMING_GAME_STRING_H

static int StringLength(const char *s) {
    int len = 0;

    while (*s) {
        len++;
        s++;
    }

    return len;
}

static char *WriteFloat(char *destination, const char *end, double value, const int precision) {
    if (value < 0) {
        if (destination < end) {
            *destination++ = '-';

            value = -value;
        }
    }

    double round_add = 0.5;
    for (int i = 0; i < precision; i++) {
        round_add /= 10;
    }
    value += round_add;

    unsigned int int_part = (unsigned int) value;
    double fraction = value - (double) int_part;

    char buffer[16];
    int i = 0;
    if (int_part == 0) {
        buffer[i++] = '0';
    } else {
        while (int_part > 0) {
            buffer[i++] = '0' + (int_part % 10);
            int_part /= 10;
        }
    }

    for (int j = i - 1; j >= 0; j--) {
        if (destination < end) {
            *destination++ = buffer[j];
        }
    }

    if (precision > 0) {
        if (destination < end) {
            *destination++ = '.';
        }

        for (int d = 0; d < precision; d++) {
            fraction *= 10.0;
            const int digit = (int) fraction;

            if (destination < end) {
                *destination++ = '0' + digit;
                fraction -= digit;
            }
        }
    }

    return destination;
}

static int IsDigit(const char c) {
    return (c >= '0' && c <= '9');
}

static void EatSpaces(char **at) {
    while (**at == ' ' || **at == '\t' || **at == '\r') {
        (*at)++;
    }
}

static void SkipLine(char **at) {
    while (**at && **at != '\n') {
        (*at)++;
    }
    if (**at == '\n') {
        (*at)++;
    }
}

static int ParseInt(char **at) {
    int sign = 1;
    if (**at == '-') {
        sign = -1;
        (*at)++;
    }

    int result = 0;
    while (IsDigit(**at)) {
        result = result * 10 + (**at - '0');
        (*at)++;
    }
    return result * sign;
}

static float ParseFloat(char **at) {
    float sign = 1.0f;
    if (**at == '-') {
        sign = -1.0f;
        (*at)++;
    }

    float result = (float) ParseInt(at);

    if (**at == '.') {
        (*at)++;
        float frac = 0.0f;
        float div = 1.0f;
        while (IsDigit(**at)) {
            frac = frac * 10.0f + (**at - '0');
            div *= 10.0f;
            (*at)++;
        }
        result += frac / div;
    }
    return result * sign;
}

#endif //GAMING_GAME_STRING_H
