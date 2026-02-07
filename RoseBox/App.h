#ifndef ROSEBOX_APP_H
#define ROSEBOX_APP_H

// App-struktur: hardkodede apper i flash, minimal heap.
struct App {
    void (*setup)();
    void (*loop)();
    const char* name;
};

// Antall apper (hjemskjerm + terminal, clock, settings, apps)
#define APP_COUNT 5

// Indeks 0 = hjemskjerm, 1 = terminal, 2 = clock, 3 = settings, 4 = apps
extern App apps[APP_COUNT];
extern App* currentApp;

// Bytt til app ved indeks. 0 = tilbake til hjemskjerm.
void launchApp(int index);

#endif
