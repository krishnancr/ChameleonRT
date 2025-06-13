# Convert binary file to C++ header
# Takes INPUT_FILE and OUTPUT_FILE variables
# Reads binary data and writes hex bytes to output file

# Open the binary file
file(READ ${INPUT_FILE} hex_content HEX)

# Convert the hex content to a comma-separated list
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," hex_content ${hex_content})

# Remove the trailing comma
string(REGEX REPLACE ",$" "" hex_content ${hex_content})

# Append the hex content to the output file
file(APPEND ${OUTPUT_FILE} ${hex_content})
