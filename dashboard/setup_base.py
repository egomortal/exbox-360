"""Thin local setup window. Every operation calls setup.py; no second importer."""
from pathlib import Path
import sys,subprocess,threading,queue,json
import tkinter as tk
from tkinter import ttk,filedialog
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT/'tools'))
from windows_drop import attach

class SetupWindow:
    def __init__(self,root,inputs=None):
        self.root=root;self.inputs=inputs;self.busy=False;self.results=queue.Queue()
        root.title('Ex-Box Setup');root.geometry('920x650')
        ttk.Label(root,text='Ex-Box Setup',font=('',22,'bold')).pack(anchor='w',padx=20,pady=(18,5))
        ttk.Label(root,text='Drop your files or folders here. Originals are copied and verified, never moved.').pack(anchor='w',padx=20)
        bar=ttk.Frame(root);bar.pack(fill='x',padx=20,pady=12)
        self.buttons=[]
        for text,command in [('Choose files',self.files),('Choose folder',self.folder),('Check inputs',lambda:self.run(['--check'])),('Prepare',lambda:self.run([])),('Build locally',lambda:self.run(['--build','host'],build=True))]:
            button=ttk.Button(bar,text=text,command=command);button.pack(side='left',padx=(0,7));self.buttons.append(button)
        self.status=tk.StringVar(value='Ready - no console connection or installation.');ttk.Label(root,textvariable=self.status).pack(anchor='w',padx=20)
        self.features=ttk.Treeview(root,columns=('status',),height=12);self.features.heading('#0',text='Feature');self.features.heading('status',text='Input status');self.features.column('#0',width=200);self.features.column('status',width=620);self.features.pack(fill='x',padx=20,pady=10)
        self.output=tk.Text(root,height=12,wrap='word');self.output.pack(fill='both',expand=True,padx=20,pady=(0,18))
        self.detach=attach(root,self.drop);root.protocol('WM_DELETE_WINDOW',self.close);root.after(80,self.poll)
        self.run(['--intake-status'])
    def close(self):
        if self.busy:self.status.set('Wait for the current local operation to finish.');return
        self.detach();self.root.destroy()
    def files(self):
        paths=filedialog.askopenfilenames();self.drop(paths)
    def folder(self):
        path=filedialog.askdirectory();self.drop([path] if path else [])
    def drop(self,paths):
        if paths:self.run(['--intake',*map(str,paths)])
    def run(self,args,build=False):
        if self.busy:self.status.set('An operation is already running; drop again when it finishes.');return
        self.busy=True;self.status.set('Working locally...');[b.state(['disabled']) for b in self.buttons]
        command=[sys.executable,str(ROOT/'setup.py'),*(['--inputs',str(self.inputs)] if self.inputs else []),*args]
        if build and sys.platform=='win32':
            project='/mnt/'+ROOT.drive[0].lower()+ROOT.as_posix()[2:]
            command=['wsl.exe','-d','Ubuntu-24.04','--cd',project,'--','python3','setup.py',*args]
        def work():
            try:
                result=subprocess.run(command,cwd=ROOT,text=True,encoding='utf-8',errors='replace',stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
                self.results.put((result.returncode,result.stdout,args))
            except Exception as error:self.results.put((1,str(error),args))
        threading.Thread(target=work,daemon=True).start()
    def poll(self):
        try:
            code,output,args=self.results.get_nowait();self.busy=False;[b.state(['!disabled']) for b in self.buttons]
            self.status.set('Complete - local files only.' if code==0 else 'Needs attention - see the result below.')
            try:
                result=json.loads(output)
                rows=result.get('files',[])
                if isinstance(rows,list):
                    lines=[row.get('status','')+': '+Path(row.get('source','')).name+' - '+row.get('kind',row.get('reason','')) for row in rows]
                else:lines=[str(result.get('status','Complete'))+'; '+str(rows)+' output files checked.']
                self.output.insert('end',('\n'.join(lines) if lines else 'Input checklist refreshed.')+'\n');self.output.see('end')
                if 'missing' in result:
                    self.features.delete(*self.features.get_children())
                    for name,value in result['missing'].items():self.features.insert('','end',text=name,values=(value,))
                if result.get('files') and isinstance(result['files'],list):
                    states={row['status'] for row in result['files']}
                    if states-{'recognized'}:self.status.set('Intake finished; some files are unknown, damaged or unverified. Originals retained.')
            except (ValueError,TypeError):self.output.insert('end',output+'\n');self.output.see('end')
        except queue.Empty:pass
        self.root.after(80,self.poll)

def main():
    root=tk.Tk();app=SetupWindow(root)
    if len(sys.argv)>1:
        def pending():
            if app.busy:root.after(100,pending)
            else:app.drop(sys.argv[1:])
        root.after(100,pending)
    root.mainloop()
if __name__=='__main__':main()
