// holmatic


#ifndef _SIGNAL_FROM_ZX_H_
#define _SIGNAL_FROM_ZX_H_
#include "esp_err.h"
#include <esp_types.h>
#include "esp_attr.h"
#include "freertos/queue.h"

#ifdef __cplusplus
extern "C" {
#endif

// call once at startup
void sfzx_init();


typedef struct {
    uint8_t  etype;   /*!< sfzx_evt_type_t */
    uint8_t  data;    /*!< for SFZX_EVTTYPE_FILE_DATA */
} sfzx_event_t;




#ifdef __cplusplus
}
#endif

#endif /* _SIGNAL_FROM_ZX_H_ */
