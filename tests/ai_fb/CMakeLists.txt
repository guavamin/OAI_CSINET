add_executable(test_ai_fb_codec
  test_ai_fb_codec.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/ai_fb_encoder.c
  ${OPENAIR1_DIR}/PHY/NR_UE_TRANSPORT/csinet_encoder.c
  ${OPENAIR2_DIR}/LAYER2/NR_MAC_gNB/ai_fb_decoder.c
  ${OPENAIR2_DIR}/LAYER2/NR_MAC_gNB/csinet_decoder.c
)

target_link_libraries(test_ai_fb_codec PRIVATE m)
target_link_libraries(test_ai_fb_codec PRIVATE asn1_nr_rrc_hdrs asn1_lte_rrc_hdrs)

add_test(NAME test_ai_fb_codec COMMAND test_ai_fb_codec)
add_dependencies(tests test_ai_fb_codec)
