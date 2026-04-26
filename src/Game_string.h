#ifndef GAMING_GAME_STRING_H
#define GAMING_GAME_STRING_H

static int StringLength(const char *s) {
    int len = 0;

    while (*s) {
        len++;
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

    unsigned int int_part = (unsigned int)value;
    double fraction = value - (double)int_part;

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
            const int digit = (int)fraction;

            if (destination < end) {
                *destination++ = '0' + digit;
                fraction -= digit;
            }
        }
    }

    return destination;
}

#endif //GAMING_GAME_STRING_H
