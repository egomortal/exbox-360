# SPDX-License-Identifier: GPL-3.0-or-later
"""Private UI delta over the existing thin setup window; no second importer."""
from pathlib import Path
import importlib.util,sys,subprocess,threading,queue,os
import tkinter as tk
from tkinter import ttk,filedialog
from upscale_integration import upscale_command
from ui_model import command,result_document,friendly_error,row_message,feature_message
ROOT=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location("reference_setup", ROOT/"setup_base.py")
reference=importlib.util.module_from_spec(spec);spec.loader.exec_module(reference)

class SetupWindow(reference.SetupWindow):
    def __init__(self,root,inputs=None):
        self.lines=[]
        self.upscale_enabled=tk.BooleanVar(root,value=False)
        self.upscale_kind=tk.StringVar(root,value="Icons")
        self.upscale_scale=tk.StringVar(root,value="2")
        self.upscale_method=tk.StringVar(root,value="Faithful")
        self.upscale_data=None
        super().__init__(root,inputs)
        self.progress=ttk.Progressbar(root,mode="indeterminate")
        self.progress.pack(fill="x",padx=20,pady=(5,0),before=self.features)
        if self.busy:self.progress.start(15)
        self.root.geometry("960x720")
        self.features.heading("status",text="Next step / status")
        images=ttk.Frame(root);images.pack(fill="x",padx=20,pady=5,before=self.features)
        ttk.Checkbutton(images,text="Create larger image copies",variable=self.upscale_enabled).pack(side="left")
        ttk.Combobox(images,textvariable=self.upscale_kind,values=("Icons","Text","Gradients","Photos"),state="readonly",width=10).pack(side="left",padx=5)
        ttk.Combobox(images,textvariable=self.upscale_scale,values=("2","4"),state="readonly",width=3).pack(side="left")
        ttk.Label(images,text="times larger").pack(side="left",padx=5)
        ttk.Combobox(images,textvariable=self.upscale_method,values=("Faithful","AI preview"),state="readonly",width=8).pack(side="left")
        ttk.Label(images,text="Faithful is recommended; AI preview requires opaque images.").pack(side="left",padx=5)

        ttk.Button(root,text="Operation details",command=self.details).pack(anchor="e",padx=20,pady=(0,8))

    def drop(self,paths):
        if not paths:return
        if not self.upscale_enabled.get():return super().drop(paths)
        if self.busy:self.status.set("Wait for the current local operation to finish.");return
        if not self.upscale_data:self.upscale_data=filedialog.askdirectory(title="Choose your Ex-Box data folder")
        if not self.upscale_data:return
        runtime=os.environ.get("EXBOX_UPSCALE_RUNTIME",str(Path(self.upscale_data)/"upscale-runtime"))
        if not (Path(runtime)/"runtime-receipt.json").is_file():self.status.set("Local image engine is not ready. Complete the documented runtime setup first.");return
        try:args=upscale_command(ROOT,paths,self.upscale_data,runtime,{"Icons":"ui","Text":"text","Gradients":"gradient","Photos":"photo"}[self.upscale_kind.get()],int(self.upscale_scale.get()),"ai" if self.upscale_method.get()=="AI preview" else "auto",sys.executable,sys.platform=="win32")
        except ValueError as error:self.status.set(str(error));return
        self.lines.clear();self.busy=True;self.status.set("Creating image copies locally...");[b.state(["disabled"]) for b in self.buttons];self.progress.start(15)
        def work():
            try:
                result=subprocess.run(args,cwd=ROOT,text=True,encoding="utf-8",errors="replace",stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=3600)
                self.results.put(("line",result.stdout));self.results.put(("done",result.returncode,["--upscale"]))
            except Exception as error:self.results.put(("line",str(error)));self.results.put(("done",1,["--upscale"]))
        threading.Thread(target=work,daemon=True).start()
    def run(self,args,build=False):
        if self.busy:
            self.status.set("An operation is already running; add more files when it finishes.");return
        try:cmd=command(ROOT,self.inputs,args,sys.executable,sys.platform=="win32",build)
        except ValueError as error:self.status.set(str(error));return
        self.busy=True;self.lines.clear()
        self.status.set("Checking inputs..." if "--check" in args or "--intake-status" in args else "Working locally...")
        for button in self.buttons:button.state(["disabled"])
        if hasattr(self,"progress"):
            self.progress.configure(mode="indeterminate",value=0);self.progress.start(15)
        def work():
            try:
                with subprocess.Popen(cmd,cwd=ROOT,text=True,encoding="utf-8",errors="replace",stdout=subprocess.PIPE,stderr=subprocess.STDOUT,bufsize=1) as process:
                    for line in process.stdout:self.results.put(("line",line))
                    code=process.wait()
                self.results.put(("done",code,args))
            except Exception as error:
                self.results.put(("line",str(error)+"\n"));self.results.put(("done",1,args))
        threading.Thread(target=work,daemon=True).start()

    def details(self):
        window=tk.Toplevel(self.root);window.title("Operation details");window.geometry("850x480")
        text=tk.Text(window,wrap="word");text.pack(fill="both",expand=True)
        text.insert("1.0","".join(self.lines));text.configure(state="disabled")

    def poll(self):
        # Bound each UI batch so a chatty build cannot starve redraw/drop events.
        for _ in range(200):
            try:event=self.results.get_nowait()
            except queue.Empty:break
            if event[0]=="line":
                line=event[1];self.lines.append(line)
                if line.startswith("Checked "):
                    self.status.set(line.strip());self.output.insert("end",line);self.output.see("end")
                continue
            code,args=event[1:];self.busy=False
            for button in self.buttons:button.state(["!disabled"])
            if hasattr(self,"progress"):
                self.progress.stop();self.progress.configure(mode="determinate",value=100 if code==0 else 0)
            output="".join(self.lines);result=result_document(output)
            self.status.set("Complete - local files only." if code==0 else friendly_error(output))
            if result and "missing" in result:
                self.features.delete(*self.features.get_children())
                for name,value in result["missing"].items():self.features.insert("","end",text=name,values=(feature_message(value),))
            rows=result.get("files",[]) if result else []
            if isinstance(rows,list):
                for row in rows:
                    self.output.insert("end",Path(row.get("source","")).name+": "+row_message(row)+"\n")
                if any(row.get("status")!="recognized" for row in rows):
                    self.status.set("Some inputs need attention. Recognized files were copied; originals are retained.")
            if code!=0:self.output.insert("end",friendly_error(output)+"\n")
            elif result and not isinstance(rows,list):self.output.insert("end",str(result.get("status","Completed"))+"\n")
            self.output.see("end")
            if result and "missing" not in result and code==0:
                # Prepare/check results have no checklist: refresh using the same
                # CLI, once the operation has fully completed.
                if "--intake-status" not in args and "--upscale" not in args:self.run(["--intake-status"])
        self.root.after(80,self.poll)

def main():
    root=tk.Tk();app=SetupWindow(root)
    if len(sys.argv)>1:
        def pending():
            if app.busy:root.after(100,pending)
            else:app.drop(sys.argv[1:])
        root.after(100,pending)
    root.mainloop()
if __name__=="__main__":main()
