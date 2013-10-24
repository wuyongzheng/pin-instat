#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <pin.H>
#include <map>

#define MYREG_INVALID   ((REG)(REG_LAST + 1))
#define MYREG_JMPTARGET ((REG)(REG_LAST + 2))
struct insrecord {
	string opcode;
	REG reg;
	int count;
	ADDRINT low;
	ADDRINT high;
};

const char *logname = "instat.log";
const char *tsvname = "instat.tsv";
FILE *logfp;
std::map<ADDRINT,insrecord> insmap;

void img_load (IMG img, void *v)
{
	fprintf(logfp, "load %s off=%08x low=%08x high=%08x start=%08x size=%08x\n",
			IMG_Name(img).c_str(),
			IMG_LoadOffset(img), IMG_LowAddress(img), IMG_HighAddress(img),
			IMG_StartAddress(img), IMG_SizeMapped(img));
}

void on_ins (ADDRINT insaddr, struct insrecord *rec, ADDRINT regval)
{
	if (rec->count == 0) {
		rec->low = rec->high = regval;
	} else {
		rec->low = min(rec->low, regval);
		rec->high = max(rec->high, regval);
	}
	rec->count ++;
}

inline bool REG_is_integer (REG reg) {
	return (reg >= REG_RBASE && reg < REG_MM_BASE);
}

void instruction (INS ins, void *v)
{
	ADDRINT addr = INS_Address(ins);

	//fprintf(log, "%x\n", addr);
	if (insmap.find(addr) != insmap.end())
		return;

	struct insrecord record;
	record.opcode = INS_Disassemble(ins);
	record.count = 0;
	record.low = record.high = 0;

	if (INS_IsBranchOrCall(ins)) {
		record.reg = MYREG_JMPTARGET;
	} else {
		record.reg = MYREG_INVALID;
		for (UINT32 regindex = 0; regindex < INS_OperandCount(ins); regindex ++) {
			if (INS_OperandRead(ins, regindex) && INS_OperandIsReg(ins, regindex) && REG_is_integer(INS_OperandReg(ins, regindex))) {
				record.reg = INS_OperandReg(ins, regindex);
				break;
			}
		}
		if (record.reg == MYREG_INVALID && INS_MaxNumRRegs(ins) > 0 && REG_is_integer(INS_RegR(ins, 0)))
			record.reg = INS_RegR(ins, 0);
	}

	//insmap.insert(addr, record);
	insmap[addr] = record;

	if (record.reg == MYREG_JMPTARGET) {
		INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins), IARG_INST_PTR,
				IARG_ADDRINT, &insmap[addr], // we assume std::map doesn't move our data.
				IARG_BRANCH_TARGET_ADDR,
				IARG_END);
	} else {
		INS_InsertCall(ins, IPOINT_BEFORE, AFUNPTR(on_ins), IARG_INST_PTR,
				IARG_ADDRINT, &insmap[addr], // we assume std::map doesn't move our data.
				record.reg == MYREG_INVALID ? IARG_ADDRINT : IARG_REG_VALUE,
				record.reg == MYREG_INVALID ? 0 : record.reg,
				IARG_END);
	}
}

void on_fini (INT32 code, void *v)
{
	fprintf(logfp, "fini %d\n", code);
	FILE *fp = fopen(tsvname, "w");
	for(std::map<ADDRINT,insrecord>::iterator ite = insmap.begin(); ite != insmap.end(); ite ++) {
		fprintf(fp, "%x\t%s\t%d\t%s\t%x\t%x\n",
				ite->first, ite->second.opcode.c_str(), ite->second.count,
				ite->second.reg == MYREG_INVALID ? "-" :
				(ite->second.reg == MYREG_JMPTARGET ? "jmp" : REG_StringShort(ite->second.reg).c_str()),
				ite->second.low, ite->second.high);
	}
	fclose(fp);
	fclose(logfp);
}

int main (int argc, char *argv[])
{
	if(PIN_Init(argc, argv)) {
		fprintf(stderr, "command line error\n");
		return 1;
	}

	logfp = fopen(logname, "w");

	//PIN_InitSymbols();

	PIN_AddFiniFunction(on_fini, 0);
	IMG_AddInstrumentFunction(img_load, NULL);
	INS_AddInstrumentFunction(instruction, NULL);

	PIN_StartProgram(); // Never returns
	return 0;
}
