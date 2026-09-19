void read(){
            DlssNrWorkingScale.set_from_config(readFloat("DlssNr", "WorkingScale"));
}
void write(){
    ini.SetValue("DlssNr", "WorkingScale", GetFloatValue(Instance()->DlssNrWorkingScale.value_for_config()).c_str());
}
