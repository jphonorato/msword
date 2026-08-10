#include "opus_x64_compat.h"

#undef native
#include <fstream>
#include <string>

/* std::filesystem is deliberately NOT used in this translation unit.  Under
   wineg++ the headers select the Windows model (path::value_type == wchar_t,
   _GLIBCXX_FILESYSTEM_IS_WINDOWS) while the out-of-line half of the library
   lives in Fedora's ELF/POSIX libstdc++.so, and the two disagree: operator/,
   .string(), create_directories and directory_iterator were each confirmed to
   corrupt the heap or silently no-op.  Paths are handled here as plain
   std::string.  See docs/port-linux/00-reconocimiento.md, "Regla de
   plataforma: std::filesystem bajo wineg++". */

#ifndef NEAR
#define NEAR
#endif
extern "C" {
#include "sdmver.h"
#include "sdm.h"
}

/* Reconstructed outputs of Microsoft's missing Dialog Editor compiler.  The
   original MKCMD parser consumes only each header's numeric cabi definition;
   compiling the contracts here lets sizeof reflect the native pointer width
   and alignment used by the x64 source modules. */
#include "about.hs"
#include "abspos.hs"
#include "apprun.hs"
#include "asgn2key.hs"
#include "asgn2mnu.hs"
#include "autosave.hs"
#include "bookmark.hs"
#include "catalog.hs"
#include "catprog.hs"
#include "catsrch.hs"
#include "char.hs"
#include "chgpr.hs"
#include "cmpfile.hs"
#include "confirmr.hs"
#include "cust.hs"
#include "doc.hs"
#include "docstat.hs"
#include "docsum.hs"
#include "edmacro.hs"
#include "edstyle.hs"
#include "footnote.hs"
#include "glsy.hs"
#include "goto.hs"
#include "header.hs"
#include "hyphen.hs"
#include "index.hs"
#include "indexent.hs"
#include "insbreak.hs"
#include "insfield.hs"
#include "insfile.hs"
#include "inspgnum.hs"
#include "inspic.hs"
#include "mrgstyle.hs"
#include "new.hs"
#include "newopen.hs"
#include "open.hs"
#include "para.hs"
#include "password.hs"
#include "pastelnk.hs"
#include "pict.hs"
#include "print.hs"
#include "printmrg.hs"
#include "prompt.hs"
#include "recorder.hs"
#include "renmacro.hs"
#include "renstyle.hs"
#include "renum.hs"
#include "replace.hs"
#include "revmark.hs"
#include "ribbon.hs"
#include "ribbon3.hs"
#include "ruler.hs"
#include "ruler3.hs"
#include "runmacro.hs"
#include "saveas.hs"
#include "search.hs"
#include "sect.hs"
#include "showvars.hs"
#include "sort.hs"
#include "spell.hs"
#include "spellmm.hs"
#include "style.hs"
#include "tablecmd.hs"
#include "tablefmt.hs"
#include "tableins.hs"
#include "tabletxt.hs"
#include "tabs.hs"
#include "thesaur.hs"
#include "toc.hs"
#include "username.hs"
#include "usrdlg.hs"
#include "viewpref.hs"
#include "vrfcnvtr.hs"

namespace {

bool WriteCabi(const std::string& directory, const char* file_name,
               const char* macro_name, const unsigned value) {
    const std::string output_path = directory + "/" + file_name;
    std::ofstream output(output_path, std::ios::trunc);
    if (!output) {
        return false;
    }
    output << "#define " << macro_name << ' ' << value << '\n';
    return output.good();
}

}  // namespace

#define WRITE_CABI(file_name, macro_name)                                      \
    success = WriteCabi(output_directory, file_name, #macro_name,              \
                        static_cast<unsigned>(macro_name)) &&                   \
              success

int main(int argc, char** argv) {
    if (argc != 2) {
        return 2;
    }
    /* The output directory is created by the invoking CMake custom command
       ("${CMAKE_COMMAND} -E make_directory"), which runs before this tool.  It
       is not created here: std::filesystem::create_directories is unusable
       under wineg++ (see the note at the top of this file), and duplicating the
       step with a hand-rolled mkdir would add a second source of truth for no
       gain. */
    const std::string output_directory(argv[1]);

    bool success = true;
    WRITE_CABI("about.hs", cabiCABABOUT);
    WRITE_CABI("abspos.hs", cabiCABABSPOS);
    WRITE_CABI("apprun.hs", cabiCABAPPRUN);
    WRITE_CABI("asgn2key.hs", cabiCABCHANGEKEYS);
    WRITE_CABI("asgn2mnu.hs", cabiCABASSIGNTOMENU);
    WRITE_CABI("autosave.hs", cabiCABAUTOSAVE);
    WRITE_CABI("bookmark.hs", cabiCABINSBOOKMARK);
    WRITE_CABI("catalog.hs", cabiCABCATALOG);
    WRITE_CABI("catprog.hs", cabiCABCATSRHPROG);
    WRITE_CABI("catsrch.hs", cabiCABCATSEARCH);
    WRITE_CABI("char.hs", cabiCABCHARACTER);
    WRITE_CABI("chgpr.hs", cabiCABCHGPR);
    WRITE_CABI("cmpfile.hs", cabiCABCMPFILE);
    WRITE_CABI("confirmr.hs", cabiCABCONFIRMREPL);
    WRITE_CABI("cust.hs", cabiCABCUSTOMIZE);
    WRITE_CABI("doc.hs", cabiCABDOCUMENT);
    WRITE_CABI("docstat.hs", cabiCABDOCSTAT);
    WRITE_CABI("docsum.hs", cabiCABDOCSUM);
    WRITE_CABI("edmacro.hs", cabiCABEDMACRO);
    WRITE_CABI("edstyle.hs", cabiCABDEFINESTYLE);
    WRITE_CABI("footnote.hs", cabiCABINSERTFTN);
    WRITE_CABI("glsy.hs", cabiCABGLOSSARY);
    WRITE_CABI("goto.hs", cabiCABGOTO);
    WRITE_CABI("header.hs", cabiCABHEADER);
    WRITE_CABI("hyphen.hs", cabiCABHYPHEN);
    WRITE_CABI("index.hs", cabiCABINDEX);
    WRITE_CABI("indexent.hs", cabiCABINDEXENTRY);
    WRITE_CABI("insbreak.hs", cabiCABINSBREAK);
    WRITE_CABI("insfield.hs", cabiCABINSFIELD);
    WRITE_CABI("insfile.hs", cabiCABINSFILE);
    WRITE_CABI("inspgnum.hs", cabiCABINSPGNUM);
    WRITE_CABI("inspic.hs", cabiCABINSPIC);
    WRITE_CABI("mrgstyle.hs", cabiCABMERGESTYLE);
    WRITE_CABI("new.hs", cabiCABNEWDOC);
    WRITE_CABI("newopen.hs", cabiCABNEWOPEN);
    WRITE_CABI("open.hs", cabiCABOPEN);
    WRITE_CABI("para.hs", cabiCABPARALOOKS);
    WRITE_CABI("password.hs", cabiCABFILEPSWD);
    WRITE_CABI("pastelnk.hs", cabiCABPASTELINK);
    WRITE_CABI("pict.hs", cabiCABFORMATPIC);
    WRITE_CABI("print.hs", cabiCABPRINT);
    WRITE_CABI("printmrg.hs", cabiCABPRINTMERGE);
    WRITE_CABI("prompt.hs", cabiCABPROMPT);
    WRITE_CABI("recorder.hs", cabiCABRECORDER);
    WRITE_CABI("renmacro.hs", cabiCABRENMACRO);
    WRITE_CABI("renstyle.hs", cabiCABRENAMESTYLE);
    WRITE_CABI("renum.hs", cabiCABRENUMPARAS);
    WRITE_CABI("replace.hs", cabiCABREPLACE);
    WRITE_CABI("revmark.hs", cabiCABREVMARKING);
    WRITE_CABI("ribbon.hs", cabiCABRIBBON);
    WRITE_CABI("ribbon3.hs", cabiCABRIBBON3);
    WRITE_CABI("ruler.hs", cabiCABRULER);
    WRITE_CABI("ruler3.hs", cabiCABRULER3);
    WRITE_CABI("runmacro.hs", cabiCABRUNMACRO);
    WRITE_CABI("saveas.hs", cabiCABSAVE);
    WRITE_CABI("search.hs", cabiCABSEARCH);
    WRITE_CABI("sect.hs", cabiCABSECTION);
    WRITE_CABI("showvars.hs", cabiCABSHOWVARS);
    WRITE_CABI("sort.hs", cabiCABSORT);
    WRITE_CABI("spell.hs", cabiCABSPELLER);
    WRITE_CABI("spellmm.hs", cabiCABSPELLERMM);
    WRITE_CABI("style.hs", cabiCABAPPLYSTYLE);
    WRITE_CABI("tablecmd.hs", cabiCABEDITTABLE);
    WRITE_CABI("tablefmt.hs", cabiCABFORMATTABLE);
    WRITE_CABI("tableins.hs", cabiCABINSERTTABLE);
    WRITE_CABI("tabletxt.hs", cabiCABTABLETOTEXT);
    WRITE_CABI("tabs.hs", cabiCABTABS);
    WRITE_CABI("thesaur.hs", cabiCABTHESAURUS);
    WRITE_CABI("toc.hs", cabiCABTOC);
    WRITE_CABI("username.hs", cabiCABUSERNAME);
    WRITE_CABI("usrdlg.hs", cabiCABUSRDLG);
    WRITE_CABI("viewpref.hs", cabiCABVIEWPREF);
    WRITE_CABI("vrfcnvtr.hs", cabiCABEXCR);
    return success ? 0 : 4;
}
