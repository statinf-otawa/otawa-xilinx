## [WIP] - OTAWA support for the following micro-architecture of Xilinx SoC's Processing Sytems 
- ARMv7-a (cortex A9) for [Xilinx Zync 7000](https://drive.google.com/drive/folders/1UwEQyxQ0KhWbEKnPWlrS3Vo266l_elHG?usp=sharing)
- ARMv8-a (cortex A53) and ARMv7-r (cortex R5F) for [Xilinx ZCU 102](https://drive.google.com/file/d/1tGO_EcPoMxhlQ6fq65_nAHACKT17WFPU/view?usp=drive_link)

### Note : 
- Files and folders's are supposed to be renamed later.
- This README will also be apdated
- This is a WIP.
- The support of the ARMv7-r (cortex R5F) for [Xilinx ZCU 102](https://drive.google.com/file/d/1tGO_EcPoMxhlQ6fq65_nAHACKT17WFPU/view?usp=drive_link) is completed. Review ongoing
### Dependencies

You need to build/install :
- the [OTAWA](https://github.com/statinf-otawa/otawa-project) framework
- [armv7t](https://github.com/statinf-otawa/armv7t/tree/master)
- otawa [loader](https://git.renater.fr/anonscm/git/otawa/otawa-arm.git) for arm 

### Build

```sh
cd otawa-xilinx
cmake . && make install
```

### Testing
```sh
owcet -s xilinxR5 /path/to/test/allButOne2 -v 
```