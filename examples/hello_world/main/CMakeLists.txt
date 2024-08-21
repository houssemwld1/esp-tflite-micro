
#
# Main component of TF Micro project 'micro_speech'.
#

#
# Main component of TF Micro project 'micro_speech'.
#

idf_component_register(
    SRCS main.cc main_functions.cc constants.cc output_handler.cc model.cc lib/lodepng.cpp image_generator.cc csi_matrices.cc stb_image_write.h mqtt.c  wifi_test_code.c   wps.c  
    PRIV_REQUIRES spi_flash
    INCLUDE_DIRS "." "lib"
)
