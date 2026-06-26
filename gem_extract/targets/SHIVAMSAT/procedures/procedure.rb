# Script Runner test script
cmd("SHIVAMSAT EXAMPLE")
wait_check("SHIVAMSAT STATUS BOOL == 'FALSE'", 5)
