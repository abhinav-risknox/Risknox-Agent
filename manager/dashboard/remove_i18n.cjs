const fs = require('fs');
const files = [
  'src/Layouts/VerticalLayouts/index.jsx',
  'src/Layouts/TwoColumnLayout/index.jsx',
  'src/Layouts/HorizontalLayout/index.jsx'
];
files.forEach(file => {
  let f = fs.readFileSync(file, 'utf8');
  f = f.replace(/import \{ withTranslation \} from ['"]react-i18next['"];/g, '');
  f = f.replace(/props\.t/g, '((v)=>v)');
  f = f.replace(/withTranslation\(\)\(([a-zA-Z]+)\)/g, '$1');
  fs.writeFileSync(file, f);
});
console.log("Done");
