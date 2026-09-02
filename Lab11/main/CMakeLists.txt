idf_component_register(SRCS "main.c" "features.c" "test_features.c"
                       INCLUDE_DIRS "."
                       REQUIRES unity
                       WHOLE_ARCHIVE)