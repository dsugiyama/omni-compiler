/* 
 * $TSUKUBA_Release: Omni OpenMP Compiler 3 $
 * $TSUKUBA_Copyright:
 *  PLEASE DESCRIBE LICENSE AGREEMENT HERE
 *  $
 */
package exc.util;

import java.io.*;
import java.util.ArrayList;
import java.util.List;

import exc.object.XobjectFile;

import exc.openmp.OMP;
import exc.openmp.OMPtranslate;

import exc.xcalablemp.XMP;
import exc.xcalablemp.XMPglobalDecl;
import exc.xcalablemp.XMPtranslate;
import exc.xcalablemp.XMPrealloc;

import xcodeml.XmLanguage;
import xcodeml.XmObj;
import xcodeml.binding.XmXcodeProgram;
import xcodeml.util.*;

/**
 * OpenMP-supported XcodeML to XcodeML translator
 */
public class omompx
{
    private static void error(String s)
    {
        System.err.println(s);
        System.exit(1);
    }
    
    private static void usage()
    {
        final String[] lines = {
            "arguments: <-xc|-xf> <-l> <-fopenmp> <-dxcode> <-ddecomp> <-dump>",
            "           <input XcodeML file>",
            "           <-o output reconstructed XcodeML file>",
            "",
            "  -xc          process XcodeML/C document.",
            "  -xf          process XcodeML/Fortran document.",
            "  -l           suppress line directive in decompiled code.",
            "  -i           output indented XcodeML.",
            "  -fopenmp     enable OpenMP translation.",
            "  -fatomicio   enable transforming Fortran IO statements to atomic operations.",
            "  -w N         set max columns to N for Fortran source.",
            "  -gnu         decompile for GNU Fortran (default).",
            "  -intel       decompile for Intel Fortran.",
            "  -decomp      output decompiled source code.",
            "",
            " Debug Options:",
            "  -d           enable output debug message.",
            "  -dxcode      output Xcode file as <input file>.x",
            "  -dump        output Xcode file and decompiled file to standard output.",
            "  -domp        enable output OpenMP translation debug message.",
        };
        
        for(String line : lines) {
            System.err.println(line);
        }
        System.exit(1);
    }
    
    public static void main(String[] args) throws Exception
    {
        String inXmlFile = null;
        String outXmlFile = null;
        String lang = "C";
        boolean openMP = false;
        boolean xcalableMP = false;
        boolean xcalableMPthreads = false;
        boolean outputXcode = false;
        boolean outputDecomp = false;
        boolean dump = false;
        boolean indent = false;
        int maxColumns = 0;
        
        for(int i = 0; i < args.length; ++i) {
            String arg = args[i];
            String narg = (i < args.length - 1) ? args[i + 1] : null;
    
            if(arg.equals("-h") || arg.equals("--help")) {
                usage();
            } else if(arg.equals("-xc")) {
                lang = "C";
            } else if(arg.equals("-xf")) {
                lang = "F";
            } else if(arg.equals("-l")) {
                XmOption.setIsSuppressLineDirective(true);
            } else if(arg.equals("-i")) {
            	indent = true;
            } else if(arg.equals("-fopenmp")) {
                openMP = true;
            } else if(arg.equals("-fxmp")) {
                xcalableMP = true;
            } else if(arg.equals("-fxmp_threads")) {
                openMP = true;
                xcalableMP = true;
                xcalableMPthreads = true;
            } else if(arg.equals("-w")) {
                if(narg == null)
                    error("needs argument after -w");
                maxColumns = Integer.parseInt(narg);
                ++i;
            } else if(arg.equals("-dxcode")) {
                outputXcode = true;
            } else if(arg.equals("-decomp")) {
                outputDecomp = true;
            } else if(arg.equals("-dump")) {
                dump = true;
            	indent = true;
                outputXcode = true;
                outputDecomp = true;
            } else if(arg.equals("-d")) {
                XmOption.setDebugOutput(true);
            } else if(arg.equals("-fatomicio")) {
                XmOption.setIsAtomicIO(true);
            } else if(arg.equals("-domp")) {
                OMP.debugFlag = true;
            } else if(arg.equals("-o")) {
                if(narg == null)
                    error("needs argument after -o");
                outXmlFile = narg;
                ++i;
            } else if(arg.equals("-gnu")) {
                XmOption.setCompilerVendor(XmOption.COMP_VENDOR_GNU);
            } else if(arg.equals("-intel")) {
                XmOption.setCompilerVendor(XmOption.COMP_VENDOR_INTEL);
            } else if(arg.startsWith("-")){
                error("unknown option " + arg);
            } else if(inXmlFile == null) {
                inXmlFile = arg;
            } else {
                error("too many arguments");
            }
        }
        
        Reader reader = null;
        Writer xmlWriter = null;
        Writer xcodeWriter = null;
        Writer decompWriter = null;
        File dir = null;
        
        if(inXmlFile == null) {
            reader = new InputStreamReader(System.in);
        } else {
            reader = new BufferedReader(new FileReader(inXmlFile));
            dir = new File(inXmlFile).getParentFile();
        }
        
        if(outXmlFile == null) {
            xmlWriter = new OutputStreamWriter(System.out);
        } else {
            xmlWriter = new BufferedWriter(new FileWriter(outXmlFile));
        }
    
        if(dump || outputXcode) {
            if(dump) {
                xcodeWriter = new OutputStreamWriter(System.out);
            } else {
                xcodeWriter = new BufferedWriter(new FileWriter(inXmlFile + ".x"));
            }
        }
    
        XmToolFactory toolFactory = new XmToolFactory(lang);
        XmOption.setLanguage(XmLanguage.valueOf(lang));
        XmOption.setIsOpenMP(openMP);
        XmOption.setIsXcalableMP(xcalableMP);
        XmOption.setIsXcalableMPthreads(xcalableMPthreads);
        
        // read XcodeML
        List<String> readErrorList = new ArrayList<String>();
        XmXcodeProgram xmProg = toolFactory.createXcodeProgram();
        XmValidator validator = toolFactory.createValidator();
    
        if(!validator.read(reader, xmProg, readErrorList)) {
            for (String error : readErrorList) {
                System.err.println(error);
                System.exit(1);
            }
        }
    
        if(inXmlFile != null) {
            reader.close();
        }
        
        String srcPath = xmProg.getSource();
        String baseName = null;
        if(dump || srcPath == null || srcPath.indexOf("<") >= 0 ) {
        	srcPath = null;
        } else {
            String fileName = new File(srcPath).getName();
            int idx = fileName.indexOf(".");
            if(idx < 0) {
                XmLog.fatal("invalid source file name : " + fileName);
            }
            baseName = fileName.substring(0, idx);
        }
        
        // translate XcodeML to Xcode
        XmXmObjToXobjectTranslator xm2xc_translator = toolFactory.createXmObjToXobjectTranslator();
        XobjectFile xobjFile = (XobjectFile)xm2xc_translator.translate((XmObj)xmProg);
        xmProg = null;
        
        if(xobjFile == null)
            System.exit(1);
        
        // Output Xcode
        if(xcodeWriter != null) {
            xobjFile.Output(xcodeWriter);
            xcodeWriter.flush();
        }
        
        System.gc();
        
        // XcalableMP translation
        if(xcalableMP) {
            XMPglobalDecl globalDecl = new XMPglobalDecl(xobjFile);
            XMPtranslate xmpTranslator = new XMPtranslate(globalDecl);
            XMPrealloc xmpReallocator = new XMPrealloc(globalDecl);

            xobjFile.iterateDef(xmpTranslator);
            XMP.exitByError();
            xobjFile.iterateDef(xmpReallocator);
            XMP.exitByError();
            globalDecl.setupGlobalConstructor();
            globalDecl.setupGlobalDestructor();
            XMP.exitByError();
            xobjFile.addHeaderLine("include \"xmp.h\"");
            xobjFile.addHeaderLine("include \"xmp_func_decl.h\"");
            xobjFile.addHeaderLine("include \"xmp_index_macro.h\"");
            xobjFile.addHeaderLine("include \"xmp_comm_macro.h\"");
            xmpTranslator.finalize();

            if(xcodeWriter != null) {
                xobjFile.Output(xcodeWriter);
                xcodeWriter.flush();
            }
        }

        // OpenMP translation
        if(openMP) {
            OMPtranslate omp_translator = new OMPtranslate(xobjFile);
            xobjFile.iterateDef(omp_translator);
            
            if(OMP.hasErrors())
                System.exit(1);
            
            omp_translator.finish();
            
            if(xcodeWriter != null) {
                xobjFile.Output(xcodeWriter);
                xcodeWriter.flush();
            }
        }
        
        if(!dump && outputXcode) {
            xcodeWriter.close();
        }
        
        // translate Xcode to XcodeML
        XmXobjectToXmObjTranslator xc2xm_translator = toolFactory.createXobjectToXmObjTranslator();
        XmXcodeProgram xmprog = (XmXcodeProgram)xc2xm_translator.translate(xobjFile);
        xobjFile = null;
    
        // Output XcodeML
        if(indent) {
            StringBuffer buf = new StringBuffer(1024 * 1024);
	        xmprog.makeTextElement(buf);
	        if(!dump && !outputDecomp) {
	            xmprog = null;
	        }
	        StringReader xmlReader = new StringReader(buf.toString());
	        buf = null;
	        XmUtil.transformToIndentedXml(2, xmlReader, xmlWriter);
	        xmlReader.close();
        } else {
        	xmprog.makeTextElement(xmlWriter);
	        if(!dump && !outputDecomp) {
	            xmprog = null;
	        }
        }
        
        xmlWriter.flush();
        
        if(outXmlFile != null) {
            xmlWriter.close();
            xmlWriter = null;
        }
        
        // Decompile
        XmDecompilerContext context = null;
        if(lang.equals("F")) {
            context = toolFactory.createDecompilerContext();
            if(maxColumns > 0)
                context.setProperty(XmDecompilerContext.KEY_MAX_COLUMNS, "" + maxColumns);
        }
        
        if(outputDecomp) {
            if(dump || srcPath == null) {
                decompWriter = new OutputStreamWriter(System.out);
            } else {
                // set decompile writer
                String newFileName = baseName + "." + (XmOption.isLanguageC() ? "c" : "F90");
                String newFileName2 = baseName + "." + (XmOption.isLanguageC() ? "c" : "f90");
                File newFile = new File(dir, newFileName);
                File newFile2 = new File(dir, newFileName2);
                
                if(newFile.exists())
                    newFile.renameTo(new File(dir, newFileName + ".i"));
                if(newFile2.exists())
                    newFile2.renameTo(new File(dir, newFileName2 + ".i"));
                
                decompWriter = new BufferedWriter(new FileWriter(newFile));
            }
            
            XmDecompiler decompiler = toolFactory.createDecompiler();
            decompiler.decompile(context, xmprog, decompWriter);
            decompWriter.flush();
    
            if(!dump && outputDecomp) {
                decompWriter.close();
            }
        }
    }
}
