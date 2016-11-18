#ifndef __XFRM_MTK_SYMBOLS__
#define __XFRM_MTK_SYMBOLS__

#define HWCRYPTO_OK		1
#define HWCRYPTO_NOMEM		0x80

extern void (*eip93Adapter_free)(unsigned int spi);
extern atomic_t esp_mtk_hardware;

#endif // __XFRM_MTK_SYMBOLS__
