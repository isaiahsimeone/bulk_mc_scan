# Bulk MC Scanner
## Usage
``` ./mcs -i INPUT_ADDRESSES -o OUTPUT_JSON [-t timeout] [-j number_of_threads]```  

Input addresses must have one ipv4 address per line  

A list of ip addresses can be obtained with MASSCAN:  
```sudo masscan 0.0.0.0/0 -oL scanned.txt --max-rate 10000 -p 25565 --exclude 255.255.255.255```  
```cat scanned.txt | awk '{print $3}' > addresses.txt```  

![Alt text](prev.png?raw=true "Preview")  

## Compilation
```$ make```
