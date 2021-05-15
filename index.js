let counter = 1;
setInterval(() => { document.querySelector("h2").innerHTML = `Hello World! ${counter} from JS`; counter++; }, 1000);

const exts = [
    "7z",
    "bmp",
    "csv",
    "doc",
    "docx",
    "gif",
    "gz",
    "jpg",
    "json",
    "mp3",
    "mp4",
    "pdf",
    "png",
    "ppt",
    "pptx",
    "sh",
    "svg",
    "tar",
    "txt",
    "wav",
    "weba",
    "webm",
    "xls",
    "xlsx",
    "zip"
];

const list = document.querySelector("#resources");

for (const ext of exts) {
    const node = document.createElement("li");
    const link = document.createElement("a");
    const filename = `test.${ext}`;
    link.innerText = filename;
    link.setAttribute("href", `./resources/${filename}`);
    node.appendChild(link);
    list.appendChild(node);
}
