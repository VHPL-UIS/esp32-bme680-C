set(SOURCES
    "esp32-C.c"
    "bme680/bme68x.c"
)

idf_component_register(SRCS ${SOURCES}
                    INCLUDE_DIRS "." "bme680"
                    EMBED_TXTFILES "cert.pem")
