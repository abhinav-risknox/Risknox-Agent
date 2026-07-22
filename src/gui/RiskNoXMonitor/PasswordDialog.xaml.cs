using System;
using System.Security.Cryptography;
using System.Windows;
using System.Windows.Input;

namespace RisknoxMonitor
{
    public partial class PasswordDialog : Window
    {
        public PasswordDialog()
        {
            InitializeComponent();
            Loaded += (s, e) =>
            {
                PwdBox.Focus();
                Keyboard.Focus(PwdBox);
            };
        }

        private async void BtnOk_Click(object sender, RoutedEventArgs e)
        {
            ErrorText.Visibility = Visibility.Collapsed;
            string password = PwdBox.Password;

            if (string.IsNullOrWhiteSpace(password))
            {
                ErrorText.Text       = "Please enter a password.";
                ErrorText.Visibility = Visibility.Visible;
                return;
            }

            BtnOk.IsEnabled     = false;
            BtnCancel.IsEnabled = false;
            BtnOk.Content       = "Verifying…";
            PwdBox.IsEnabled    = false;

            try
            {
                // Offload to a thread-pool thread so the UI stays responsive
                bool valid = await System.Threading.Tasks.Task.Run(
                    () => VerifyPassword(password));

                if (valid)
                {
                    DialogResult = true;
                }
                else
                {
                    ErrorText.Text       = "Incorrect password. Please try again.";
                    ErrorText.Visibility = Visibility.Visible;
                    PwdBox.Clear();
                }
            }
            catch (Exception ex)
            {
                ErrorText.Text       = $"Verification error: {ex.Message}";
                ErrorText.Visibility = Visibility.Visible;
                PwdBox.Clear();
            }
            finally
            {
                BtnOk.IsEnabled     = true;
                BtnCancel.IsEnabled = true;
                BtnOk.Content       = "Stop Agent";
                PwdBox.IsEnabled    = true;
                if (ErrorText.Visibility == Visibility.Visible)
                    PwdBox.Focus();
            }
        }

        private void BtnCancel_Click(object sender, RoutedEventArgs e)
        {
            DialogResult = false;
        }

        private bool VerifyPassword(string password)
        {
            if (string.IsNullOrEmpty(password)) return false;

            // Hardcoded SHA-256 hash for "saab#Q7m!L2x"
            const string HardcodedHash = "cce32b7e52c0a9d703a959b7f99f915e4f1dc37a47fb01337b615d16153ea60c";

            using (var sha256 = System.Security.Cryptography.SHA256.Create())
            {
                byte[] bytes = System.Text.Encoding.UTF8.GetBytes(password);
                byte[] hash = sha256.ComputeHash(bytes);
                string hashString = BitConverter.ToString(hash).Replace("-", "").ToLowerInvariant();

                return CryptographicOperations.FixedTimeEquals(
                    System.Text.Encoding.UTF8.GetBytes(HardcodedHash),
                    System.Text.Encoding.UTF8.GetBytes(hashString));
            }
        }
    }
}
