/*
 * $TSUKUBA_Release: $
 * $TSUKUBA_Copyright:
 *  $
 */

package exc.xcalablemp;

import exc.block.*;
import exc.object.*;
import java.util.Vector;

public class XMPtemplate extends XMPobject {
  // defined in xmp_constant.h
  public final static int DUPLICATION	= 100;
  public final static int BLOCK		= 101;
  public final static int CYCLIC	= 102;

  private boolean		_isFixed;
  private boolean		_isDistributed;
  private XMPnodes		_ontoNodes;
  private Vector<XobjInt>	_ontoNodesIndexVector;
  private Vector<Integer>	_distMannerVector;
  private Vector<Xobject>	_lowerVector;

  public XMPtemplate(String name, int dim, Ident descId) {
    super(XMPobject.TEMPLATE, name, dim, descId);

    _isFixed = false;
    _isDistributed = false;
    _ontoNodes = null;
    _ontoNodesIndexVector = new Vector<XobjInt>();
    _distMannerVector = new Vector<Integer>();

    for (int i = 0; i < dim; i++) {
      _ontoNodesIndexVector.add(null);
      _distMannerVector.add(null);
    }

    _lowerVector = new Vector<Xobject>();
  }

  public void setIsFixed() {
    _isFixed = true;
  }

  public boolean isFixed() {
    return _isFixed;
  }

  public void setIsDistributed() {
    _isDistributed = true;
  }

  public boolean isDistributed() {
    return _isDistributed;
  }

  public void setOntoNodes(XMPnodes nodes) {
    _ontoNodes = nodes;
  }

  public XMPnodes getOntoNodes() {
    return _ontoNodes;
  }

  public void setOntoNodesIndexAt(int nodesDimIdx, int templateDimIdx) {
    _ontoNodesIndexVector.setElementAt(Xcons.IntConstant(nodesDimIdx), templateDimIdx);
  }

  public XobjInt getOntoNodesIndexAt(int index) {
    return _ontoNodesIndexVector.get(index);
  }

  public void setDistMannerAt(int manner, int index) {
    _distMannerVector.setElementAt(new Integer(manner), index);
  }

  public int getDistMannerAt(int index) throws XMPexception {
    if (!_isDistributed) {
      throw new XMPexception("template " + getName() + " is not distributed");
    }

    return _distMannerVector.get(index).intValue();
  }

  public String getDistMannerStringAt(int index) throws XMPexception {
    if (!_isDistributed) {
      throw new XMPexception("template " + getName() + " is not distributed");
    }

    switch (getDistMannerAt(index)) {
      case DUPLICATION:
        return new String("DUPLICATION");
      case BLOCK:
        return new String("BLOCK");
      case CYCLIC:
        return new String("CYCLIC");
      default:
        throw new XMPexception("unknown distribute manner");
    }
  }

  public void addLower(Xobject lower) {
    _lowerVector.add(lower);
  }

  public Xobject getLowerAt(int index) {
    return _lowerVector.get(index);
  }

  public static void translateTemplate(XobjList templateDecl, XMPglobalDecl globalDecl,
                                       boolean isLocalPragma, PragmaBlock pb) throws XMPexception {
    // local parameters
    BlockList funcBlockList = null;
    XMPsymbolTable localXMPsymbolTable = null;
    if (isLocalPragma) {
      funcBlockList = XMPlocalDecl.findParentFunctionBlock(pb).getBody();
      localXMPsymbolTable = XMPlocalDecl.declXMPsymbolTable(pb);
    }

    // check name collision
    String templateName = templateDecl.getArg(0).getString();
    if (isLocalPragma) {
      XMPlocalDecl.checkObjectNameCollision(templateName, funcBlockList, localXMPsymbolTable);
    }
    else {
      globalDecl.checkObjectNameCollision(templateName);
    }

    // declare template desciptor
    Ident templateDescId = null;
    if (isLocalPragma) {
      templateDescId = XMPlocalDecl.addObjectId(XMP.DESC_PREFIX_ + templateName, pb);
    }
    else {
      templateDescId = globalDecl.declStaticIdent(XMP.DESC_PREFIX_ + templateName, Xtype.voidPtrType);
    }

    // declare template object
    int templateDim = 0;
    for (XobjArgs i = templateDecl.getArg(1).getArgs(); i != null; i = i.nextArgs()) templateDim++;
    if (templateDim > XMP.MAX_DIM) {
      throw new XMPexception("template dimension should be less than " + (XMP.MAX_DIM + 1));
    }

    XMPtemplate templateObject = new XMPtemplate(templateName, templateDim, templateDescId);
    if (isLocalPragma) {
      localXMPsymbolTable.putXMPobject(templateObject);
    }
    else {
      globalDecl.putXMPobject(templateObject);
    }

    // create function call
    boolean templateIsFixed = true;
    XobjList templateArgs = Xcons.List(templateDescId.getAddr(), Xcons.IntConstant(templateDim));
    for (XobjArgs i = templateDecl.getArg(1).getArgs(); i != null; i = i.nextArgs()) {
      Xobject templateSpec = i.getArg();
      if (templateSpec == null) {
        templateIsFixed = false;

        templateObject.addLower(null);
        templateObject.addUpper(null);
      }
      else {
        Xobject templateLower = templateSpec.left();
        Xobject templateUpper = templateSpec.right();

        templateArgs.add(Xcons.Cast(Xtype.longlongType, templateLower));
        templateArgs.add(Xcons.Cast(Xtype.longlongType, templateUpper));

        templateObject.addLower(templateLower);
        templateObject.addUpper(templateUpper);
      }
    }

    String constructorName = new String("_XMP_init_template_");
    if (templateIsFixed) {
      templateObject.setIsFixed();
      constructorName += "FIXED";
    }
    else {
      constructorName += "UNFIXED";
    }

    if (isLocalPragma) {
      XMPlocalDecl.addConstructorCall(constructorName, templateArgs, pb, globalDecl);
      XMPlocalDecl.insertDestructorCall("_XMP_finalize_template", Xcons.List(templateDescId.Ref()), pb, globalDecl);
    }
    else {
      globalDecl.addGlobalInitFuncCall(constructorName, templateArgs);
    }
  }
}
