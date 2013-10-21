#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pin.H>
#include <map>

struct insrecord {
	string opcode;
	REG reg;
	int count;
	ADDRINT low;
	ADDRINT high;
};

const char *imgname = "ld-linux.so.2";
const char *logname = "instat.log";
FILE *log;
ADDRINT imglow = 0;
ADDRINT imghigh = 0;
bool imgloaded = false;
std::map<ADDRINT,insrecord> insmap;

void img_load (IMG img, void *v)
{
	fprintf(log, "load %s off=%08x low=%08x high=%08x start=%08x size=%08x\n",
			IMG_Name(img).c_str(),
			IMG_LoadOffset(img), IMG_LowAddress(img), IMG_HighAddress(img),
			IMG_StartAddress(img), IMG_SizeMapped(img));
	if (IMG_Name(img).rfind(imgname) != std::string::npos) {
		fprintf(log, "matched\n");
		imglow = IMG_LowAddress(img);
		imghigh = IMG_HighAddress(img);
		imgloaded = true;
	}
}

void img_unload (IMG img, void *v)
{
	if (IMG_Name(img).rfind(imgname) != std::string::npos) {
		fprintf(log, "unload matched\n");
		imgloaded = false;
	}
}

void on_ins (ADDRINT insaddr, ADDRINT regval)
{
	if (insmap[insaddr].count == 0) {
		insmap[insaddr].low = insmap[insaddr].high = regval;
	} else {
		insmap[insaddr].low = min(insmap[insaddr].low, insaddr);
		insmap[insaddr].high = max(insmap[insaddr].high, insaddr);
	}
	insmap[insaddr].count ++;
}

void instruction (INS ins, void *v)
{
	ADDRINT addr = INS_Address(ins);
	//fprintf(log, "%x\n", addr);

	if (imgloaded && addr >= imglow && addr <= imghigh) {
		//fprintf(log, "%x\n", addr);
		if (insmap.find(addr) == insmap.end()) {
			struct insrecord record;
			record.opcode = INS_Disassemble(ins);
			record.count = 0;

			UINT32 regindex = 0;
			while (regindex < INS_OperandCount(ins)) {
				if (INS_OperandRead(ins, regindex) && INS_OperandIsReg(ins, regindex))
					break;
				regindex ++;
			}
			if (regindex < INS_OperandCount(ins)) {
				record.reg = INS_OperandReg(ins, regindex);
				INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins), IARG_INST_PTR, IARG_REG_VALUE, record.reg, IARG_END);
			} else {
				record.reg = REG_INVALID();
				INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins), IARG_INST_PTR, IARG_ADDRINT, 0, IARG_END);
			}

			//insmap.insert(addr, record);
			insmap[addr] = record;
		}
	}
}

void on_fini (INT32 code, void *v)
{
	fprintf(log, "fini %d\n", code);
	for(std::map<ADDRINT,insrecord>::iterator ite = insmap.begin(); ite != insmap.end(); ite ++) {
		fprintf(log, "%x\t%s\t%d\t%s\t%x\t%x\n", ite->first, ite->second.opcode.c_str(), ite->second.count, REG_StringShort(ite->second.reg).c_str(), ite->second.low, ite->second.high);
	}
}

int main (int argc, char *argv[])
{
	if(PIN_Init(argc, argv)) {
		fprintf(stderr, "command line error\n");
		return 1;
	}

	log = fopen(logname, "w");

	PIN_InitSymbols();

	PIN_AddFiniFunction(on_fini, 0);
	IMG_AddInstrumentFunction(img_load, NULL);
	IMG_AddUnloadFunction(img_unload, NULL);
	INS_AddInstrumentFunction(instruction, NULL);

	PIN_StartProgram(); // Never returns
	return 0;
}
