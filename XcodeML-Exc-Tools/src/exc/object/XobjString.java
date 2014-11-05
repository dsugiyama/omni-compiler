/* 
 * $TSUKUBA_Release: Omni OpenMP Compiler 3 $
 * $TSUKUBA_Copyright:
 *  PLEASE DESCRIBE LICENSE AGREEMENT HERE
 *  $
 */
package exc.object;
import exc.block.Block;

public class XobjString extends XobjConst
{
    String value;

    public XobjString(Xcode code, Xtype type, String value, String fkind)
    {
        super(code, type, fkind);
        this.value = value.intern();
    }

    public XobjString(Xcode code, Xtype type, String value)
    {
        this(code, type, value, null);
    }

    public XobjString(Xcode code, String value)
    {
        this(code, null, value);
    }

    @Override
    public String getString()
    {
        return value;
    }

    @Override
    public String getSym()
    {
        return value;
    }

    // used by xcalablemp package
    public void setSym(String newValue)
    {
        value = newValue;
    }

    @Override
    public String getName()
    {
        return value;
    }
    
    public void setName(String newValue)
    {
        value = newValue;
    }

    @Override
    public Xobject cfold(XobjectDef def, Block block)
    {
      if (value == null) 
        return this.copy();

      Ident ident = def.findVarIdent(value);
      return ident.cfold(def, block);
    }

    @Override
    public Xobject copy()
    {
        return copyTo(new XobjString(code, type, value, getFkind()));
    }

    @Override
    public boolean equals(Xobject x)
    {
        return super.equals(x) && value.equals(x.getString());
    }

    @Override
    public String toString()
    {
        return "(" + OpcodeName() + " " + value + ")";
    }
}
